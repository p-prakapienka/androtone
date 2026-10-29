#pragma once

// Private building blocks shared by the Analog and Wave synths: control-rate parameter
// smoothing, an exponential ADSR, LFO shapes, the multimode filter and voice allocation
// (poly with stealing, mono and legato with a note stack). Allocation-free after construction.

namespace rb::synth
{

// Modulation and coefficients are updated every controlBlock samples.
constexpr int controlBlock = 16;

// Notes that can sound at once, plus spare voices that let stolen notes fade out.
constexpr int maxPolyphony = 8;
constexpr int voicePoolSize = 12;

inline bool changed (float a, float b) { return ! juce::exactlyEqual (a, b); }

// Pade approximation of tanh, exact at the clamp points.
inline float softClip (float x)
{
    x = juce::jlimit (-3.0f, 3.0f, x);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// Small fast PRNG for noise, drift and sample & hold (one per voice).
struct Rng
{
    uint32_t state = 0x9e3779b9u;
    void seed (uint32_t s) { state = s != 0 ? s : 0x9e3779b9u; }
    uint32_t nextInt()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    float nextBipolar() { return (float) (nextInt() >> 8) * (2.0f / 16777216.0f) - 1.0f; }
    float nextUnipolar() { return (float) (nextInt() >> 8) * (1.0f / 16777216.0f); }
};

// One-pole DC blocker (~8 Hz). Saturating asymmetric waveforms creates DC, which analog
// synths remove with AC coupling.
struct DcBlocker
{
    float x1 = 0.0f, y1 = 0.0f, r = 0.999f;

    void prepare (double sampleRate) { r = 1.0f - dsp::twoPi * 8.0f / (float) sampleRate; }
    void reset() { x1 = y1 = 0.0f; }
    float process (float x)
    {
        y1 = x - x1 + r * y1;
        x1 = x;
        return y1;
    }
};

//==============================================================================
// Holds the latest parameter values (targets) and per-parameter smoothed copies that advance
// once per control block. Discrete and time parameters are taken over directly.
class ParamSmoother
{
public:
    void configure (const DeviceSpec& spec)
    {
        numParams = juce::jmin ((int) spec.params.size(), maxDeviceParams);
        for (int i = 0; i < numParams; ++i)
        {
            auto& p = spec.params[(size_t) i];
            smooth[(size_t) i] = ! (p.isDiscrete() || p.unit == ParamUnit::seconds);
            target[(size_t) i] = current[(size_t) i] = p.defaultValue;
        }
    }

    void prepare (double sampleRate, float timeSeconds = 0.02f)
    {
        for (int len = 0; len <= controlBlock; ++len)
            coeffs[(size_t) len] = 1.0f - std::exp (-(float) len / ((float) sampleRate * timeSeconds));
    }

    void setTargets (const float* values, int n)
    {
        n = juce::jmin (n, numParams);
        for (int i = 0; i < n; ++i)
            target[(size_t) i] = values[i];
        if (! initialised)
        {
            snap();
            initialised = true;
        }
    }

    void snap() { current = target; }

    void advance (int len)
    {
        const float c = coeffs[(size_t) juce::jlimit (0, controlBlock, len)];
        for (int i = 0; i < numParams; ++i)
        {
            auto& cur = current[(size_t) i];
            const float t = target[(size_t) i];
            if (! smooth[(size_t) i])
                cur = t;
            else if (changed (cur, t))
            {
                cur += (t - cur) * c;
                if (std::abs (t - cur) < 1.0e-6f * (1.0f + std::abs (t)))
                    cur = t;
            }
        }
    }

    float operator[] (int index) const { return current[(size_t) index]; }
    float getTarget (int index) const { return target[(size_t) index]; }
    int getChoice (int index) const { return juce::roundToInt (target[(size_t) index]); }

private:
    std::array<float, maxDeviceParams> target {}, current {};
    std::array<bool, maxDeviceParams> smooth {};
    std::array<float, controlBlock + 1> coeffs {};
    int numParams = 0;
    bool initialised = false;
};

//==============================================================================
// Coefficients of an analog-style exponential ADSR, shared by all voices of a synth.
struct AdsrShape
{
    float attackCoeff = 1.0f, decayCoeff = 0.0f, sustain = 1.0f, releaseCoeff = 0.0f;

    void set (double sampleRate, float attackS, float decayS, float sustainLevel, float releaseS)
    {
        if (! (changed (attackS, lastA) || changed (decayS, lastD) || changed (sustainLevel, lastS) || changed (releaseS, lastR)))
            return;
        lastA = attackS; lastD = decayS; lastS = sustainLevel; lastR = releaseS;

        const auto sr = (float) sampleRate;
        // Attack charges towards 1.3 and reaches 1.0 after attackS (an RC curve, like the
        // analog originals); decay reaches the sustain level within ~1 %, release falls 60 dB.
        attackCoeff = 1.0f - std::exp (-1.466f / (juce::jmax (0.0005f, attackS) * sr));
        decayCoeff = std::exp (-4.6f / (juce::jmax (0.001f, decayS) * sr));
        sustain = juce::jlimit (0.0f, 1.0f, sustainLevel);
        releaseCoeff = std::exp (-6.9f / (juce::jmax (0.001f, releaseS) * sr));
    }

private:
    float lastA = -1.0f, lastD = -1.0f, lastS = -1.0f, lastR = -1.0f;
};

class Adsr
{
public:
    enum class Stage { idle, attack, decay, sustain, release };

    void noteOn()  { stage = Stage::attack; }
    void noteOff() { if (stage != Stage::idle) stage = Stage::release; }
    void reset()   { stage = Stage::idle; level = 0.0f; }
    bool isActive() const { return stage != Stage::idle; }
    bool isReleasing() const { return stage == Stage::release; }
    float getLevel() const { return level; }

    float next (const AdsrShape& s)
    {
        switch (stage)
        {
            case Stage::idle: break;
            case Stage::attack:
                level += (1.3f - level) * s.attackCoeff;
                if (level >= 1.0f)
                {
                    level = 1.0f;
                    stage = Stage::decay;
                }
                break;
            case Stage::decay:
                level = s.sustain + (level - s.sustain) * s.decayCoeff;
                if (level - s.sustain < 1.0e-4f)
                {
                    level = s.sustain;
                    stage = s.sustain > 1.0e-4f ? Stage::sustain : Stage::idle;
                }
                break;
            case Stage::sustain:
                level += (s.sustain - level) * 0.001f; // follows sustain changes without steps
                break;
            case Stage::release:
                level *= s.releaseCoeff;
                if (level < 1.0e-4f)
                {
                    level = 0.0f;
                    stage = Stage::idle;
                }
                break;
        }
        return level;
    }

private:
    Stage stage = Stage::idle;
    float level = 0.0f;
};

//==============================================================================
enum LfoShape { lfoSine = 0, lfoTriangle, lfoSawUp, lfoSawDown, lfoSquare, lfoRandom };

inline juce::StringArray lfoShapeNames() { return { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random" }; }

inline float lfoValue (int shape, float phase, float held)
{
    switch (shape)
    {
        case lfoSine:     return std::sin (dsp::twoPi * phase);
        case lfoTriangle: return 1.0f - 4.0f * std::abs (phase - 0.5f);
        case lfoSawUp:    return 2.0f * phase - 1.0f;
        case lfoSawDown:  return 1.0f - 2.0f * phase;
        case lfoSquare:   return phase < 0.5f ? 1.0f : -1.0f;
        case lfoRandom:
        default:          return held;
    }
}

// Per-voice LFO state, advanced once per control block.
// Tempo-synced LFO divisions, in beats per cycle.
inline const juce::StringArray& lfoDivisionNames()
{
    static const juce::StringArray names { "4 Bars", "2 Bars", "1 Bar", "1/2", "1/2T", "1/4", "1/4T",
                                           "1/8", "1/8T", "1/16", "1/16T", "1/32" };
    return names;
}

inline double lfoDivisionBeats (int index)
{
    static const double beats[] = { 16.0, 8.0, 4.0, 2.0, 4.0 / 3.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    return beats[juce::jlimit (0, 11, index)];
}

struct VoiceLfo
{
    float phase = 0.0f, held = 0.0f, smoothedHeld = 0.0f, fade = 1.0f;

    void start (float startPhase, float firstHeld)
    {
        phase = startPhase;
        held = smoothedHeld = firstHeld;
        fade = 0.0f;
    }

    // Returns the value at the start of the block and advances by len samples.
    float tick (int shape, float increment, float fadeIncrement, int len, Rng& rng)
    {
        // Random steps are slightly rounded (~2 ms) so they don't click.
        smoothedHeld += (held - smoothedHeld) * juce::jmin (1.0f, 0.15f * (float) len / (float) controlBlock);
        const float value = lfoValue (shape, phase, smoothedHeld) * fade;
        phase += increment * (float) len;
        if (phase >= 1.0f)
        {
            phase -= std::floor (phase);
            held = rng.nextBipolar();
        }
        fade = juce::jmin (1.0f, fade + fadeIncrement * (float) len);
        return value;
    }
};

//==============================================================================
enum FilterType { filterLp12 = 0, filterLp24, filterBp, filterHp };

inline juce::StringArray filterTypeNames() { return { "LP 12", "LP 24", "BP", "HP" }; }

// Two cascaded TPT state-variable stages (dsp::Svf). LP 24 uses both; the first stage is
// Butterworth-damped, the second carries the resonance.
struct MultiFilter
{
    dsp::Svf a, b;

    void prepare (double sr) { a.prepare (sr); b.prepare (sr); }
    void reset() { a.reset(); b.reset(); }

    void set (int type, float cutoffHz, float resonance)
    {
        b.set (cutoffHz, resonance * 0.97f);
        if (type == filterLp24)
            a.set (cutoffHz, 0.3f);
    }

    float process (int type, float x)
    {
        switch (type)
        {
            case filterLp24: return b.tick (a.tick (x).low).low;
            case filterBp:   { const auto o = b.tick (x); return o.band * b.getK(); }
            case filterHp:   return b.tick (x).high;
            case filterLp12:
            default:         return b.tick (x).low;
        }
    }
};

// Converts a value in note numbers (log frequency) to Hz within the audible range.
inline float noteToHz (float note) { return juce::jlimit (16.0f, 20000.0f, dsp::midiToHz (note)); }
inline float hzToNote (float hz) { return 69.0f + 12.0f * std::log2 (juce::jmax (1.0f, hz) / 440.0f); }

inline juce::StringArray octaveNames() { return { "-2", "-1", "0", "+1", "+2" }; }
inline juce::StringArray voiceModeNames() { return { "Poly", "Mono", "Legato" }; }

//==============================================================================
// Control values every voice needs to start notes; filled by the synth each control block.
struct CommonControl
{
    // MIDI expression: smoothed pitch bend (semitones) and pressure per note / channel.
    float bend = 0.0f, bendTarget = 0.0f;
    float channelPressure = 0.0f;
    std::array<float, 128> notePressure {};
    double tempo = 120.0;

    float pressureFor (int note) const
    {
        return juce::jmax (channelPressure, notePressure[(size_t) juce::jlimit (0, 127, note)]);
    }

    void advanceBend (int len, float sampleRate)
    {
        bend += (bendTarget - bend) * juce::jmin (1.0f, (float) len / (sampleRate * 0.008f));
    }

    bool glideOn = false;
    float glideCoeff = 1.0f;       // per control block
    bool lfoRetrigger = false;
    float globalLfoPhase = 0.0f;   // free-running phase new voices join when not retriggered
};

// Note, envelope, glide, LFO and fade-out state shared by the voices of both synths.
struct VoiceCore
{
    Adsr ampEnv, modEnv;
    VoiceLfo lfo;
    Rng rng;
    const CommonControl* common = nullptr;
    float sampleRate = 48000.0f;
    float currentNote = 60.0f; // gliding pitch
    float velocity = 1.0f;
    float killGain = 1.0f, killStep = 0.0f;
    int note = 60;
    uint32_t stamp = 0;
    bool killing = false;

    void prepareCore (double sr, uint32_t seed, const CommonControl* c)
    {
        sampleRate = (float) sr;
        rng.seed (seed);
        common = c;
    }

    bool isActive() const { return ampEnv.isActive(); }
    bool isReleased() const { return killing || ampEnv.isReleasing(); }
    bool isKilling() const { return killing; }
    float getLevel() const { return ampEnv.getLevel() * killGain; }
    int getNote() const { return note; }
    uint32_t getStamp() const { return stamp; }

    void release()
    {
        ampEnv.noteOff();
        modEnv.noteOff();
    }

    void kill (bool fast)
    {
        const float step = 1.0f / (sampleRate * (fast ? 0.001f : 0.004f));
        if (! killing)
        {
            killing = true;
            killGain = 1.0f;
            killStep = step;
        }
        else
        {
            killStep = juce::jmax (killStep, step);
        }
    }

    void resetCore()
    {
        ampEnv.reset();
        modEnv.reset();
        killing = false;
        killGain = 1.0f;
    }

    void startCore (int n, float vel, float glideFrom, uint32_t s)
    {
        resetCore();
        note = n;
        currentNote = (common->glideOn && glideFrom >= 0.0f) ? glideFrom : (float) n;
        velocity = vel;
        stamp = s;
        lfo.start (common->lfoRetrigger ? 0.0f : common->globalLfoPhase, rng.nextBipolar());
        ampEnv.noteOn();
        modEnv.noteOn();
    }

    void retriggerCore (int n, float vel, bool glide, uint32_t s)
    {
        note = n;
        if (! (glide && common->glideOn))
            currentNote = (float) n;
        velocity = vel;
        stamp = s;
        if (common->lfoRetrigger)
            lfo.start (0.0f, rng.nextBipolar());
        ampEnv.noteOn();
        modEnv.noteOn();
    }

    void legatoTo (int n, bool glide)
    {
        note = n;
        if (! (glide && common->glideOn))
            currentNote = (float) n;
    }

    void advanceGlide()
    {
        const auto target = (float) note;
        if (changed (currentNote, target))
        {
            currentNote += (target - currentNote) * common->glideCoeff;
            if (std::abs (target - currentNote) < 0.001f)
                currentNote = target;
        }
    }

    // Per-sample fade applied while a stolen voice dies; false once it has finished.
    bool nextKillGain (float& gain)
    {
        if (! killing)
        {
            gain = 1.0f;
            return true;
        }
        killGain -= killStep;
        gain = juce::jmax (0.0f, killGain);
        return killGain > 0.0f;
    }
};

//==============================================================================
enum class VoiceMode { poly = 0, mono, legato };

// Voice allocation shared by both synths. Voice must provide:
//   bool isActive() const;  bool isReleased() const;  bool isKilling() const;
//   float getLevel() const; int getNote() const; uint32_t getStamp() const;
//   void start (int note, float velocity, float glideFromNote, uint32_t stamp); // fresh voice
//   void retrigger (int note, float velocity, bool glide, uint32_t stamp);     // envelopes restart from their level
//   void legatoTo (int note, bool glide);
//   void release(); void kill (bool fast); void hardReset();
// kill() fades the voice out within a few milliseconds (1 ms when fast) and frees it.
template <typename Voice>
class VoiceAllocator
{
public:
    std::array<Voice, voicePoolSize> voices;

    void setMode (VoiceMode m)
    {
        if (m == mode)
            return;
        for (auto& v : voices)
            if (v.isActive() && ! v.isReleased())
                v.release();
        numHeld = 0;
        mode = m;
    }

    VoiceMode getMode() const { return mode; }

    void noteOn (int note, float velocity)
    {
        ++stamp;
        if (mode == VoiceMode::poly)
            polyNoteOn (note, velocity);
        else
            monoNoteOn (note, velocity);
        lastNote = (float) note;
    }

    void noteOff (int note)
    {
        if (mode == VoiceMode::poly)
        {
            for (auto& v : voices)
                if (v.isActive() && ! v.isReleased() && v.getNote() == note)
                    v.release();
            return;
        }

        removeHeld (note);
        auto& v = voices[0];
        if (! v.isActive() || v.isReleased() || v.getNote() != note)
            return;

        if (numHeld > 0)
            v.legatoTo (held[(size_t) numHeld - 1], true);
        else
            v.release();
    }

    void killAll()
    {
        for (auto& v : voices)
            if (v.isActive())
                v.kill (true);
        numHeld = 0;
    }

    void resetAll()
    {
        for (auto& v : voices)
            v.hardReset();
        numHeld = 0;
    }

private:
    void polyNoteOn (int note, float velocity)
    {
        // The same note retriggers its voice (keeps fast repeats from piling up).
        for (auto& v : voices)
            if (v.isActive() && ! v.isKilling() && v.getNote() == note)
            {
                v.retrigger (note, velocity, false, stamp);
                return;
            }

        int sounding = 0;
        for (auto& v : voices)
            if (v.isActive() && ! v.isKilling())
                ++sounding;

        if (sounding >= maxPolyphony)
        {
            // Steal the quietest released voice, otherwise the oldest held one.
            Voice* victim = nullptr;
            for (auto& v : voices)
                if (v.isActive() && ! v.isKilling() && v.isReleased()
                    && (victim == nullptr || v.getLevel() < victim->getLevel()))
                    victim = &v;
            if (victim == nullptr)
                for (auto& v : voices)
                    if (v.isActive() && ! v.isKilling() && (victim == nullptr || v.getStamp() < victim->getStamp()))
                        victim = &v;
            if (victim != nullptr)
                victim->kill (false);
        }

        Voice* target = nullptr;
        for (auto& v : voices)
            if (! v.isActive())
            {
                target = &v;
                break;
            }

        if (target == nullptr)
        {
            // Every spare voice is still fading: cut the quietest one short.
            for (auto& v : voices)
                if (v.isKilling() && (target == nullptr || v.getLevel() < target->getLevel()))
                    target = &v;
            if (target == nullptr)
                target = &voices[0];
            target->hardReset();
        }

        target->start (note, velocity, lastNote, stamp);
    }

    void monoNoteOn (int note, float velocity)
    {
        removeHeld (note);
        const bool overlapping = numHeld > 0;
        if (numHeld == (int) held.size())
        {
            std::copy (held.begin() + 1, held.end(), held.begin());
            --numHeld;
        }
        held[(size_t) numHeld++] = note;

        auto& v = voices[0];
        if (v.isKilling())
            v.hardReset();

        if (! v.isActive())
            v.start (note, velocity, lastNote, stamp);
        else if (mode == VoiceMode::legato && overlapping && ! v.isReleased())
            v.legatoTo (note, true);
        else
            v.retrigger (note, velocity, true, stamp);
    }

    void removeHeld (int note)
    {
        for (int i = 0; i < numHeld; ++i)
            if (held[(size_t) i] == note)
            {
                for (int j = i; j < numHeld - 1; ++j)
                    held[(size_t) j] = held[(size_t) j + 1];
                --numHeld;
                return;
            }
    }

    VoiceMode mode = VoiceMode::poly;
    std::array<int, 16> held {};
    int numHeld = 0;
    uint32_t stamp = 0;
    float lastNote = -1.0f;
};

} // namespace rb::synth
