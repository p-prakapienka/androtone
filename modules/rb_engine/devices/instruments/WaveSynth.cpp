#include "AnalogWaveCommon.h"

namespace rb
{
namespace wave
{

// Parameter indices, in spec order.
enum Param
{
    osc1Table, osc1Pos, osc1Octave, osc1Level,
    osc2Table, osc2Pos, osc2Octave, osc2Semi, osc2Detune, osc2Level,
    subLevel, noiseLevel,
    filterType, cutoff, resonance, filterEnv, keyTrack, drive,
    ampAttack, ampDecay, ampSustain, ampRelease, modAttack, modDecay, modSustain, modRelease,
    lfoShape, lfoRate, lfoAmount, lfoDest, lfoFade, lfoRetrig,
    velToFilter, velToAmp, modEnvPos, modEnvPitch,
    voiceMode, glide, unison, unisonDetune, unisonSpread, transpose, volume,
    numParams
};

enum LfoDest { destPosition = 0, destPos1, destPos2, destPitch, destFilter, destAmp };

constexpr int maxUnison = 7;
constexpr float voiceGain = 0.42f;

struct Control : synth::CommonControl
{
    const WaveTableBank* bank = nullptr;
    int table1 = 0, table2 = 1;
    float pos1 = 0.0f, pos2 = 0.0f, osc1Semis = 0.0f, osc2Semis = 0.0f;
    float level1 = 1.0f, level2 = 0.0f, sub = 0.0f, noise = 0.0f;
    int filterType = synth::filterLp12;
    float cutoffNote = 90.0f, resonance = 0.0f, envAmount = 0.0f, keyTrack = 0.0f;
    float driveIn = 0.5f, driveOut = 2.0f, gainComp = 1.0f;
    int lfoShape = 0, lfoDest = destPosition;
    float lfoIncrement = 0.0f, lfoAmount = 0.0f, lfoFadeIncrement = 1.0f;
    float velToFilter = 0.0f, velToAmp = 0.0f, modEnvPos = 0.0f, modEnvPitch = 0.0f;
    int unison = 1;
    float maxRatio = 1.0f;
    std::array<float, maxUnison> ratios {}, gainsL {}, gainsR {};
    synth::AdsrShape amp, mod;
};

// One wavetable oscillator's view of the bank for a control block.
struct TableRead
{
    const float* a = nullptr;
    const float* b = nullptr;
    float frameFrac = 0.0f, size = 0.0f;

    void set (const WaveTableBank& bank, int table, float position, float increment)
    {
        const float fp = juce::jlimit (0.0f, 1.0f, position) * (float) (WaveTableBank::numFrames - 1);
        const int frame = juce::jmin ((int) fp, WaveTableBank::numFrames - 2);
        frameFrac = fp - (float) frame;
        const int level = WaveTableBank::levelFor (increment);
        a = bank.row (table, frame, level);
        b = bank.row (table, frame + 1, level);
        size = (float) WaveTableBank::levelSize (level);
    }

    float read (float phase) const
    {
        const float index = phase * size;
        const int i = (int) index;
        const float f = index - (float) i;
        const float sa = a[i] + f * (a[i + 1] - a[i]);
        const float sb = b[i] + f * (b[i + 1] - b[i]);
        return sa + frameFrac * (sb - sa);
    }
};

struct Voice : synth::VoiceCore
{
    const Control* ctl = nullptr;
    std::array<float, maxUnison> phases1 {}, phases2 {};
    float subPhase = 0.0f, lastGain = -1.0f;
    synth::MultiFilter filterL, filterR;
    synth::DcBlocker dcL, dcR;

    void prepare (double sr, uint32_t seed, const Control* c)
    {
        prepareCore (sr, seed, c);
        ctl = c;
        filterL.prepare (sr);
        filterR.prepare (sr);
        dcL.prepare (sr);
        dcR.prepare (sr);
    }

    void hardReset()
    {
        resetCore();
        filterL.reset();
        filterR.reset();
        dcL.reset();
        dcR.reset();
    }

    void start (int n, float vel, float glideFrom, uint32_t s)
    {
        startCore (n, vel, glideFrom, s);
        filterL.reset();
        filterR.reset();
        dcL.reset();
        dcR.reset();
        const bool randomPhase = ctl->unison > 1;
        for (int k = 0; k < maxUnison; ++k)
        {
            phases1[(size_t) k] = randomPhase ? rng.nextUnipolar() : 0.0f;
            phases2[(size_t) k] = randomPhase ? rng.nextUnipolar() : 0.0f;
        }
        subPhase = 0.0f;
        lastGain = -1.0f;
    }

    void retrigger (int n, float vel, bool glide, uint32_t s) { retriggerCore (n, vel, glide, s); }

    static float wrap (float p) { return p >= 1.0f ? p - 1.0f : p; }

    void render (float* left, float* right, int len)
    {
        const auto& c = *ctl;
        const auto& bank = *c.bank;

        advanceGlide();

        const float lfoRaw = lfo.tick (c.lfoShape, c.lfoIncrement, c.lfoFadeIncrement, len, rng);
        const float lfoAmt = c.lfoAmount;
        const float modLevel = modEnv.getLevel();

        float pitch = currentNote + modLevel * c.modEnvPitch;
        if (c.lfoDest == destPitch)
            pitch += lfoRaw * lfoAmt * lfoAmt * 12.0f;

        const float inc1 = juce::jlimit (0.0f, 0.45f, dsp::midiToHz (pitch + c.osc1Semis) / sampleRate);
        const float inc2 = juce::jlimit (0.0f, 0.45f, dsp::midiToHz (pitch + c.osc2Semis) / sampleRate);

        float pos1 = c.pos1 + modLevel * c.modEnvPos, pos2 = c.pos2 + modLevel * c.modEnvPos;
        const float lfoPos = lfoRaw * lfoAmt * 0.5f;
        if (c.lfoDest == destPosition || c.lfoDest == destPos1) pos1 += lfoPos;
        if (c.lfoDest == destPosition || c.lfoDest == destPos2) pos2 += lfoPos;

        const int unison = c.unison;
        const bool use1 = c.level1 > 0.0f, use2 = c.level2 > 0.0f;
        TableRead read1, read2;
        std::array<float, maxUnison> incs1 {}, incs2 {}, g1L {}, g1R {}, g2L {}, g2R {};
        if (use1)
            read1.set (bank, c.table1, pos1, inc1 * c.maxRatio);
        if (use2)
            read2.set (bank, c.table2, pos2, inc2 * c.maxRatio);
        for (int k = 0; k < unison; ++k)
        {
            const auto i = (size_t) k;
            incs1[i] = inc1 * c.ratios[i];
            incs2[i] = inc2 * c.ratios[i];
            g1L[i] = c.gainsL[i] * c.level1;
            g1R[i] = c.gainsR[i] * c.level1;
            g2L[i] = c.gainsL[i] * c.level2;
            g2R[i] = c.gainsR[i] * c.level2;
        }

        float cutoffNote = c.cutoffNote + c.keyTrack * (currentNote - 60.0f) + c.envAmount * modLevel
                           + c.velToFilter * (velocity - 1.0f) * 30.0f;
        if (c.lfoDest == destFilter)
            cutoffNote += lfoRaw * lfoAmt * 48.0f;
        const float cutoffHz = synth::noteToHz (cutoffNote);
        filterL.set (c.filterType, cutoffHz, c.resonance);
        filterR.set (c.filterType, cutoffHz, c.resonance);

        float gain = voiceGain * c.gainComp * (1.0f - c.velToAmp * (1.0f - velocity * velocity));
        if (c.lfoDest == destAmp)
            gain *= 1.0f - lfoAmt * 0.5f * (1.0f - lfoRaw);
        if (lastGain < 0.0f)
            lastGain = gain;
        const float gainStep = (gain - lastGain) / (float) len;
        float g = lastGain;
        lastGain = gain;

        const float subInc = inc1 * 0.5f, subGain = c.sub, noiseGain = c.noise;
        const float* sine = bank.sine();
        const float driveIn = c.driveIn, driveOut = c.driveOut;
        const int type = c.filterType;

        for (int i = 0; i < len; ++i)
        {
            float l = 0.0f, r = 0.0f;

            if (use1)
                for (int k = 0; k < unison; ++k)
                {
                    const auto u = (size_t) k;
                    const float s = read1.read (phases1[u]);
                    phases1[u] = wrap (phases1[u] + incs1[u]);
                    l += s * g1L[u];
                    r += s * g1R[u];
                }

            if (use2)
                for (int k = 0; k < unison; ++k)
                {
                    const auto u = (size_t) k;
                    const float s = read2.read (phases2[u]);
                    phases2[u] = wrap (phases2[u] + incs2[u]);
                    l += s * g2L[u];
                    r += s * g2R[u];
                }

            float mono = 0.0f;
            if (subGain > 0.0f)
            {
                const float index = subPhase * (float) WaveTableBank::sineSize;
                const int si = (int) index;
                mono += subGain * (sine[si] + (index - (float) si) * (sine[si + 1] - sine[si]));
            }
            subPhase = wrap (subPhase + subInc);
            if (noiseGain > 0.0f)
                mono += noiseGain * rng.nextBipolar();

            l = dcL.process (synth::softClip ((l + mono) * driveIn) * driveOut);
            r = dcR.process (synth::softClip ((r + mono) * driveIn) * driveOut);
            l = filterL.process (type, l);
            r = filterR.process (type, r);

            float fade;
            const bool alive = nextKillGain (fade);
            const float a = ampEnv.next (c.amp) * fade * g;
            modEnv.next (c.mod);
            g += gainStep;

            left[i] += l * a;
            if (right != nullptr)
                right[i] += r * a;

            if (! alive)
            {
                hardReset();
                break;
            }
            if (! ampEnv.isActive())
                break;
        }
    }
};

} // namespace wave

//==============================================================================
DeviceSpec WaveSynth::createSpec()
{
    using P = ParamSpec;
    DeviceSpec spec;
    spec.typeId = "wave";
    spec.displayName = "Wave";
    spec.groups = { "Osc", "Filter", "Env", "LFO", "Mod", "Global" };

    const auto& tables = wave::WaveTableBank::getTableNames();
    auto& p = spec.params;
    // Osc
    p.push_back (P::choice ("osc1Table", "Osc 1 Table", tables, 0, "Osc"));
    p.push_back (P::continuous ("osc1Pos", "Osc 1 Position", 0.0f, 1.0f, 0.5f, ParamUnit::percent, 0.0f, "Osc"));
    p.push_back (P::choice ("osc1Octave", "Osc 1 Octave", synth::octaveNames(), 2, "Osc"));
    p.push_back (P::continuous ("osc1Level", "Osc 1 Level", 0.0f, 1.0f, 0.8f, ParamUnit::percent, 0.0f, "Osc"));
    p.push_back (P::choice ("osc2Table", "Osc 2 Table", tables, 1, "Osc"));
    p.push_back (P::continuous ("osc2Pos", "Osc 2 Position", 0.0f, 1.0f, 0.4f, ParamUnit::percent, 0.0f, "Osc"));
    p.push_back (P::choice ("osc2Octave", "Osc 2 Octave", synth::octaveNames(), 2, "Osc"));
    p.push_back (P::continuous ("osc2Semi", "Osc 2 Semi", -12.0f, 12.0f, 0.0f, ParamUnit::semitones, 0.0f, "Osc"));
    p.push_back (P::continuous ("osc2Detune", "Osc 2 Detune", -50.0f, 50.0f, 7.0f, ParamUnit::cents, 0.0f, "Osc"));
    p.push_back (P::continuous ("osc2Level", "Osc 2 Level", 0.0f, 1.0f, 0.4f, ParamUnit::percent, 0.0f, "Osc"));
    p.push_back (P::continuous ("subLevel", "Sub", 0.0f, 1.0f, 0.0f, ParamUnit::percent, 0.0f, "Osc"));
    p.push_back (P::continuous ("noiseLevel", "Noise", 0.0f, 1.0f, 0.0f, ParamUnit::percent, 0.0f, "Osc"));
    // Filter
    p.push_back (P::choice ("filterType", "Filter Type", synth::filterTypeNames(), synth::filterLp12, "Filter"));
    p.push_back (P::continuous ("cutoff", "Cutoff", 20.0f, 20000.0f, 5000.0f, ParamUnit::hertz, 1000.0f, "Filter"));
    p.push_back (P::continuous ("resonance", "Resonance", 0.0f, 1.0f, 0.1f, ParamUnit::percent, 0.0f, "Filter"));
    p.push_back (P::continuous ("filterEnv", "Env Amount", -1.0f, 1.0f, 0.2f, ParamUnit::percent, 0.0f, "Filter"));
    p.push_back (P::continuous ("keyTrack", "Key Track", 0.0f, 1.0f, 0.5f, ParamUnit::percent, 0.0f, "Filter"));
    p.push_back (P::continuous ("drive", "Drive", 0.0f, 1.0f, 0.0f, ParamUnit::percent, 0.0f, "Filter"));
    // Env
    p.push_back (P::continuous ("ampAttack", "Attack", 0.001f, 10.0f, 0.005f, ParamUnit::seconds, 0.4f, "Env"));
    p.push_back (P::continuous ("ampDecay", "Decay", 0.001f, 10.0f, 0.8f, ParamUnit::seconds, 0.6f, "Env"));
    p.push_back (P::continuous ("ampSustain", "Sustain", 0.0f, 1.0f, 0.75f, ParamUnit::percent, 0.0f, "Env"));
    p.push_back (P::continuous ("ampRelease", "Release", 0.001f, 15.0f, 0.35f, ParamUnit::seconds, 0.8f, "Env"));
    p.push_back (P::continuous ("modAttack", "Mod Attack", 0.001f, 10.0f, 0.002f, ParamUnit::seconds, 0.4f, "Env"));
    p.push_back (P::continuous ("modDecay", "Mod Decay", 0.001f, 10.0f, 0.6f, ParamUnit::seconds, 0.6f, "Env"));
    p.push_back (P::continuous ("modSustain", "Mod Sustain", 0.0f, 1.0f, 0.3f, ParamUnit::percent, 0.0f, "Env"));
    p.push_back (P::continuous ("modRelease", "Mod Release", 0.001f, 15.0f, 0.4f, ParamUnit::seconds, 0.8f, "Env"));
    // LFO
    p.push_back (P::choice ("lfoShape", "LFO Shape", synth::lfoShapeNames(), 0, "LFO"));
    p.push_back (P::continuous ("lfoRate", "LFO Rate", 0.05f, 30.0f, 1.0f, ParamUnit::hertz, 3.0f, "LFO"));
    p.push_back (P::continuous ("lfoAmount", "LFO Amount", 0.0f, 1.0f, 0.0f, ParamUnit::percent, 0.0f, "LFO"));
    p.push_back (P::choice ("lfoDest", "LFO Target", { "Position", "Position 1", "Position 2", "Pitch", "Filter", "Amp" },
                            wave::destPosition, "LFO"));
    p.push_back (P::continuous ("lfoFade", "LFO Fade In", 0.0f, 5.0f, 0.0f, ParamUnit::seconds, 1.0f, "LFO"));
    p.push_back (P::toggle ("lfoRetrig", "LFO Retrigger", false, "LFO"));
    // Mod
    p.push_back (P::continuous ("velToFilter", "Vel > Filter", 0.0f, 1.0f, 0.3f, ParamUnit::percent, 0.0f, "Mod"));
    p.push_back (P::continuous ("velToAmp", "Vel > Amp", 0.0f, 1.0f, 0.5f, ParamUnit::percent, 0.0f, "Mod"));
    p.push_back (P::continuous ("modEnvPos", "Mod Env > Position", -1.0f, 1.0f, 0.0f, ParamUnit::percent, 0.0f, "Mod"));
    p.push_back (P::continuous ("modEnvPitch", "Mod Env > Pitch", -24.0f, 24.0f, 0.0f, ParamUnit::semitones, 0.0f, "Mod"));
    // Global
    p.push_back (P::choice ("voiceMode", "Voices", synth::voiceModeNames(), 0, "Global"));
    p.push_back (P::continuous ("glide", "Glide", 0.0f, 2.0f, 0.0f, ParamUnit::seconds, 0.3f, "Global"));
    p.push_back (P::choice ("unison", "Unison", { "1", "2", "3", "4", "5", "6", "7" }, 0, "Global"));
    p.push_back (P::continuous ("unisonDetune", "Unison Detune", 0.0f, 1.0f, 0.25f, ParamUnit::percent, 0.0f, "Global"));
    p.push_back (P::continuous ("unisonSpread", "Unison Width", 0.0f, 1.0f, 0.6f, ParamUnit::percent, 0.0f, "Global"));
    p.push_back (P::continuous ("transpose", "Transpose", -24.0f, 24.0f, 0.0f, ParamUnit::semitones, 0.0f, "Global"));
    p.push_back (P::continuous ("volume", "Volume", -70.0f, 6.0f, 0.0f, ParamUnit::decibels, 0.0f, "Global"));

    jassert ((int) p.size() == wave::numParams);
    return spec;
}

//==============================================================================
struct WaveSynth::Impl
{
    double sampleRate = 48000.0;
    synth::ParamSmoother params;
    synth::VoiceAllocator<wave::Voice> voices;
    wave::Control ctl;
    float lastVolume = -1.0f;
    int lastUnison = -1;
    float lastDetune = -1.0f, lastSpread = -1.0f;

    Impl()
    {
        ctl.bank = &wave::WaveTableBank::get();
        params.configure (WaveSynth::createSpec());
        prepare (48000.0);
    }

    void prepare (double sr)
    {
        sampleRate = sr;
        params.prepare (sr);
        uint32_t seed = 0x7654321u;
        for (auto& v : voices.voices)
            v.prepare (sr, seed += 0x9e3779b1u, &ctl);
        reset();
    }

    void reset()
    {
        voices.resetAll();
        params.snap();
        ctl.globalLfoPhase = 0.0f;
        lastVolume = -1.0f;
        lastUnison = -1;
    }

    void updateUnison (int count, float detune, float spread)
    {
        if (count == lastUnison && ! synth::changed (detune, lastDetune) && ! synth::changed (spread, lastSpread))
            return;
        lastUnison = count;
        lastDetune = detune;
        lastSpread = spread;

        auto& c = ctl;
        c.unison = count;
        c.maxRatio = 1.0f;
        const float norm = 1.0f / std::sqrt ((float) count);
        const float cents = detune * detune * 60.0f;
        for (int k = 0; k < count; ++k)
        {
            const float offset = count == 1 ? 0.0f : 2.0f * (float) k / (float) (count - 1) - 1.0f;
            const auto i = (size_t) k;
            c.ratios[i] = std::pow (2.0f, offset * cents / 1200.0f);
            c.maxRatio = juce::jmax (c.maxRatio, c.ratios[i]);
            float l, r;
            dsp::panGains (offset * spread, l, r);
            c.gainsL[i] = l * norm;
            c.gainsR[i] = r * norm;
        }
    }

    void updateControl (int len)
    {
        using namespace wave;
        params.advance (len);
        const auto sr = (float) sampleRate;
        auto& c = ctl;

        const float trans = std::round (params[transpose]);
        c.table1 = params.getChoice (osc1Table);
        c.table2 = params.getChoice (osc2Table);
        c.pos1 = params[osc1Pos];
        c.pos2 = params[osc2Pos];
        c.osc1Semis = (float) (params.getChoice (osc1Octave) - 2) * 12.0f + trans;
        c.osc2Semis = (float) (params.getChoice (osc2Octave) - 2) * 12.0f + std::round (params[osc2Semi])
                      + params[osc2Detune] * 0.01f + trans;
        c.level1 = params[osc1Level];
        c.level2 = params[osc2Level];
        c.sub = params[subLevel] * 0.6f;
        c.noise = params[noiseLevel] * 0.5f;

        c.filterType = params.getChoice (filterType);
        c.cutoffNote = synth::hzToNote (params[cutoff]);
        c.resonance = params[resonance];
        c.envAmount = params[filterEnv] * 60.0f;
        c.keyTrack = params[keyTrack];
        const float driveGain = dsp::dbToGain (params[drive] * 30.0f);
        c.driveIn = 0.5f * driveGain;
        c.driveOut = 2.0f / std::pow (driveGain, 0.4f);
        c.gainComp = c.filterType == synth::filterHp ? 1.0f : 1.0f - 0.3f * c.resonance;

        c.amp.set (sampleRate, params[ampAttack], params[ampDecay], params[ampSustain], params[ampRelease]);
        c.mod.set (sampleRate, params[modAttack], params[modDecay], params[modSustain], params[modRelease]);

        c.lfoShape = params.getChoice (lfoShape);
        c.lfoDest = params.getChoice (lfoDest);
        c.lfoAmount = params[lfoAmount];
        c.lfoIncrement = params[lfoRate] / sr;
        const float fade = params[lfoFade];
        c.lfoFadeIncrement = fade > 0.001f ? 1.0f / (fade * sr) : 1.0f;
        c.lfoRetrigger = params.getChoice (lfoRetrig) != 0;
        c.globalLfoPhase += c.lfoIncrement * (float) len;
        c.globalLfoPhase -= std::floor (c.globalLfoPhase);

        c.velToFilter = params[velToFilter];
        c.velToAmp = params[velToAmp];
        c.modEnvPos = params[modEnvPos];
        c.modEnvPitch = params[modEnvPitch];

        const float glideTime = params[glide];
        c.glideOn = glideTime > 0.0005f;
        c.glideCoeff = c.glideOn ? 1.0f - std::exp (-3.0f * (float) len / (glideTime * sr)) : 1.0f;

        updateUnison (juce::jlimit (1, maxUnison, params.getChoice (unison) + 1), params[unisonDetune], params[unisonSpread]);
    }

    void renderRange (float* left, float* right, int count)
    {
        while (count > 0)
        {
            const int len = juce::jmin (synth::controlBlock, count);
            updateControl (len);

            for (auto& v : voices.voices)
                if (v.isActive())
                    v.render (left, right, len);

            const float vol = params[wave::volume];
            const float gain = vol <= -69.5f ? 0.0f : dsp::dbToGain (vol);
            if (lastVolume < 0.0f)
                lastVolume = gain;
            const float step = (gain - lastVolume) / (float) len;
            float g = lastVolume;
            for (int i = 0; i < len; ++i)
            {
                g += step;
                left[i] *= g;
                if (right != nullptr)
                    right[i] *= g;
            }
            lastVolume = gain;

            left += len;
            if (right != nullptr)
                right += len;
            count -= len;
        }
    }

    void process (juce::AudioBuffer<float>& dry, const NoteEvent* events, int numEvents, int numSamples)
    {
        juce::ScopedNoDenormals noDenormals;
        voices.setMode ((synth::VoiceMode) juce::jlimit (0, 2, params.getChoice (wave::voiceMode)));
        updateControl (0); // notes at offset 0 start with this block's settings

        float* left = dry.getWritePointer (0);
        float* right = dry.getNumChannels() > 1 ? dry.getWritePointer (1) : nullptr;

        dsp::renderWithEvents (events, numEvents, numSamples,
            [this] (const NoteEvent& e)
            {
                const int pitch = juce::jlimit (0, 127, e.pitch);
                if (e.isNoteOn())
                    voices.noteOn (pitch, juce::jlimit (0.0f, 1.0f, e.velocity));
                else
                    voices.noteOff (pitch);
            },
            [&] (int start, int count) { renderRange (left + start, right != nullptr ? right + start : nullptr, count); });
    }
};

WaveSynth::WaveSynth() : impl (std::make_unique<Impl>()) {}
WaveSynth::~WaveSynth() = default;

void WaveSynth::prepare (double sampleRate, int) { impl->prepare (sampleRate); }
void WaveSynth::reset() { impl->reset(); }
void WaveSynth::setParameters (const float* values, int numValues) { impl->params.setTargets (values, numValues); }
void WaveSynth::allNotesOff() { impl->voices.killAll(); }

void WaveSynth::process (juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>&, const NoteEvent* events,
                         int numEvents, int numSamples)
{
    impl->process (dry, events, numEvents, numSamples);
}

} // namespace rb
