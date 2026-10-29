#include "AnalogWaveCommon.h"

namespace rb
{
namespace analog
{

// Parameter indices, in spec order.
enum Param
{
    osc1Shape, osc1Octave, osc2Shape, osc2Octave, osc2Semi, osc2Detune, oscMix, pulseWidth, subLevel,
    noiseLevel, fmAmount,
    filterType, cutoff, resonance, filterEnv, keyTrack, drive,
    ampAttack, ampDecay, ampSustain, ampRelease, modAttack, modDecay, modSustain, modRelease,
    lfoShape, lfoRate, lfoSync, lfoDivision, lfoAmount, lfoDest, lfoFade, lfoRetrig,
    velToFilter, velToAmp, modEnvPitch, modEnvOsc2, modEnvFm,
    voiceMode, glide, drift, spread, transpose, volume,
    numParams
};

enum LfoDest { destPitch = 0, destFilter, destAmp, destPulseWidth, destShape, destOscMix };

constexpr float voiceGain = 0.42f;

// Values shared by all voices for one control block.
struct Control : synth::CommonControl
{
    float shape1 = 2.0f, shape2 = 2.0f, pulseWidth = 0.5f, mix = 0.5f, sub = 0.0f, noise = 0.0f;
    float fm = 0.0f, fmEnv = 0.0f;
    float osc1Semis = 0.0f, osc2Semis = 0.0f;
    int filterType = synth::filterLp24;
    float cutoffNote = 90.0f, resonance = 0.0f, envAmount = 0.0f, keyTrack = 0.0f;
    float driveIn = 0.5f, driveOut = 2.0f, gainComp = 1.0f;
    int lfoShape = 0, lfoDest = destFilter;
    float lfoIncrement = 0.0f, lfoAmount = 0.0f, lfoFadeIncrement = 1.0f;
    float velToFilter = 0.0f, velToAmp = 0.0f, modEnvPitch = 0.0f, modEnvOsc2 = 0.0f;
    float drift = 0.0f, spread = 0.0f;
    synth::AdsrShape amp, mod;
};

// PolyBLEP/BLAMP oscillator morphing sine -> triangle -> saw -> pulse (shape 0..3). All shapes
// are phase-aligned so the crossfades don't cancel.
struct MorphOsc
{
    float phase = 0.0f, increment = 0.0f;

    static float wrap (float p) { return p - std::floor (p); }

    static float blep (float t, float dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    static float blamp (float t, float dt)
    {
        if (t < dt) { t = t / dt - 1.0f; return -t * t * t * (1.0f / 3.0f); }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt + 1.0f; return t * t * t * (1.0f / 3.0f); }
        return 0.0f;
    }

    static float shapeAt (int shape, float p, float dt, float pw)
    {
        switch (shape)
        {
            case 0:
                return std::sin (dsp::twoPi * p);
            case 1:
            {
                float q = p + 0.25f;
                if (q >= 1.0f) q -= 1.0f;
                float q2 = q + 0.5f;
                if (q2 >= 1.0f) q2 -= 1.0f;
                return 1.0f - 4.0f * std::abs (q - 0.5f) + 4.0f * dt * (blamp (q, dt) - blamp (q2, dt));
            }
            case 2:
            {
                float q = p + 0.5f;
                if (q >= 1.0f) q -= 1.0f;
                return 2.0f * q - 1.0f - blep (q, dt);
            }
            default:
            {
                float y = p < pw ? 1.0f : -1.0f;
                y += blep (p, dt);
                float q = p + 1.0f - pw;
                if (q >= 1.0f) q -= 1.0f;
                y -= blep (q, dt);
                return (y - (2.0f * pw - 1.0f)) * 0.7f;
            }
        }
    }

    float next (int shape, float frac, float pw, float pm, bool usePm)
    {
        const float p = usePm ? wrap (phase + pm) : phase;
        float y = shapeAt (shape, p, increment, pw);
        if (frac > 1.0e-4f)
            y += frac * (shapeAt (shape + 1, p, increment, pw) - y);
        phase += increment;
        if (phase >= 1.0f)
            phase -= 1.0f;
        return y;
    }
};

static void splitShape (float shape, int& index, float& frac)
{
    shape = juce::jlimit (0.0f, 3.0f, shape);
    index = juce::jmin (2, (int) shape);
    frac = shape - (float) index;
}

struct Voice : synth::VoiceCore
{
    const Control* ctl = nullptr;
    MorphOsc osc1, osc2;
    float subPhase = 0.0f;
    synth::MultiFilter filter;
    synth::DcBlocker dcBlocker;
    float panPosition = 0.0f, lastGainL = 0.0f, lastGainR = 0.0f;
    float driftStaticPitch = 0.0f, driftStaticCutoff = 0.0f, driftState = 0.0f, driftTarget = 0.0f;
    int driftCountdown = 0;

    void prepare (double sr, uint32_t seed, const Control* c)
    {
        prepareCore (sr, seed, c);
        ctl = c;
        filter.prepare (sr);
        dcBlocker.prepare (sr);
    }

    void hardReset()
    {
        resetCore();
        filter.reset();
        dcBlocker.reset();
    }

    void start (int n, float vel, float glideFrom, uint32_t s)
    {
        startCore (n, vel, glideFrom, s);
        filter.reset();
        dcBlocker.reset();
        osc1.phase = 0.0f;
        osc2.phase = ctl->drift > 0.0f ? rng.nextUnipolar() : 0.0f;
        subPhase = 0.0f;
        static constexpr float pans[] = { -0.2f, 0.25f, -0.55f, 0.6f, -0.85f, 0.9f, -0.4f, 0.4f };
        panPosition = pans[s % 8];
        driftStaticPitch = rng.nextBipolar();
        driftStaticCutoff = rng.nextBipolar();
        lastGainL = lastGainR = -1.0f; // no ramp on the first block
    }

    void retrigger (int n, float vel, bool glide, uint32_t s) { retriggerCore (n, vel, glide, s); }

    void render (float* left, float* right, int len)
    {
        const auto& c = *ctl;

        advanceGlide();

        // Slow random walk for the analog drift.
        driftCountdown -= len;
        if (driftCountdown <= 0)
        {
            driftTarget = rng.nextBipolar();
            driftCountdown = (int) (sampleRate * (0.15f + 0.5f * rng.nextUnipolar()));
        }
        driftState += (driftTarget - driftState) * juce::jmin (1.0f, (float) len / (sampleRate * 0.25f));

        const float lfoRaw = lfo.tick (c.lfoShape, c.lfoIncrement, c.lfoFadeIncrement, len, rng);
        const float lfoAmt = c.lfoAmount;
        const float modLevel = modEnv.getLevel();

        float pitch = currentNote + c.bend + modLevel * c.modEnvPitch
                      + c.drift * (0.10f * driftStaticPitch + 0.08f * driftState);
        if (c.lfoDest == destPitch)
            pitch += lfoRaw * lfoAmt * lfoAmt * 12.0f;

        osc1.increment = juce::jlimit (0.0f, 0.45f, dsp::midiToHz (pitch + c.osc1Semis) / sampleRate);
        osc2.increment = juce::jlimit (0.0f, 0.45f, dsp::midiToHz (pitch + c.osc2Semis + modLevel * c.modEnvOsc2) / sampleRate);
        const float subIncrement = osc1.increment * 0.5f;

        float shape1 = c.shape1, shape2 = c.shape2, pw = c.pulseWidth, mix = c.mix;
        switch (c.lfoDest)
        {
            case destShape:      shape1 += lfoRaw * lfoAmt * 1.5f; shape2 += lfoRaw * lfoAmt * 1.5f; break;
            case destPulseWidth: pw += lfoRaw * lfoAmt * 0.45f; break;
            case destOscMix:     mix += lfoRaw * lfoAmt * 0.5f; break;
            default: break;
        }
        pw = juce::jlimit (0.05f, 0.95f, pw);
        mix = juce::jlimit (0.0f, 1.0f, mix);
        int s1, s2;
        float f1, f2;
        splitShape (shape1, s1, f1);
        splitShape (shape2, s2, f2);

        const float fm = juce::jlimit (0.0f, 1.0f, c.fm + modLevel * c.fmEnv);
        const float pmDepth = fm * fm * 1.2f; // phase modulation depth in cycles
        const bool usePm = pmDepth > 1.0e-5f;

        float cutoffNote = c.cutoffNote + c.keyTrack * (currentNote - 60.0f) + c.envAmount * modLevel
                           + c.velToFilter * (velocity - 1.0f) * 30.0f + c.drift * 1.5f * driftStaticCutoff;
        if (c.lfoDest == destFilter)
            cutoffNote += lfoRaw * lfoAmt * 48.0f;
        cutoffNote += c.pressureFor (note) * 24.0f; // pressure opens the filter by up to two octaves
        filter.set (c.filterType, synth::noteToHz (cutoffNote), c.resonance);

        float gain = voiceGain * c.gainComp * (1.0f - c.velToAmp * (1.0f - velocity * velocity));
        if (c.lfoDest == destAmp)
            gain *= 1.0f - lfoAmt * 0.5f * (1.0f - lfoRaw);

        float panL, panR;
        dsp::panGains (panPosition * c.spread, panL, panR);
        const float gainL = gain * panL, gainR = gain * panR;
        if (lastGainL < 0.0f)
        {
            lastGainL = gainL;
            lastGainR = gainR;
        }
        const float stepL = (gainL - lastGainL) / (float) len, stepR = (gainR - lastGainR) / (float) len;
        float gl = lastGainL, gr = lastGainR;
        lastGainL = gainL;
        lastGainR = gainR;

        const float mix1 = 1.0f - mix, mix2 = mix, subGain = c.sub, noiseGain = c.noise;
        const float driveIn = c.driveIn, driveOut = c.driveOut;
        const int type = c.filterType;

        for (int i = 0; i < len; ++i)
        {
            const float o2 = osc2.next (s2, f2, pw, 0.0f, false);
            const float o1 = osc1.next (s1, f1, pw, o2 * pmDepth, usePm);

            float x = o1 * mix1 + o2 * mix2;

            if (subGain > 0.0f)
            {
                float sq = subPhase < 0.5f ? 1.0f : -1.0f;
                sq += MorphOsc::blep (subPhase, subIncrement);
                float q = subPhase + 0.5f;
                if (q >= 1.0f) q -= 1.0f;
                sq -= MorphOsc::blep (q, subIncrement);
                x += sq * subGain;
            }
            subPhase += subIncrement;
            if (subPhase >= 1.0f)
                subPhase -= 1.0f;

            if (noiseGain > 0.0f)
                x += rng.nextBipolar() * noiseGain;

            x = dcBlocker.process (synth::softClip (x * driveIn) * driveOut);
            const float y = filter.process (type, x);

            float fade;
            const bool alive = nextKillGain (fade);
            const float a = ampEnv.next (c.amp) * fade;
            modEnv.next (c.mod);

            gl += stepL;
            gr += stepR;
            const float out = y * a;
            left[i] += out * gl;
            if (right != nullptr)
                right[i] += out * gr;

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

} // namespace analog

//==============================================================================
DeviceSpec AnalogSynth::createSpec()
{
    using P = ParamSpec;
    DeviceSpec spec;
    spec.typeId = "analog";
    spec.displayName = "Analog";
    spec.groups = { "Osc", "Filter", "Env", "LFO", "Mod", "Global" };

    auto& p = spec.params;
    // Osc
    p.push_back (P::continuous ("osc1Shape", "Osc 1 Shape", 0.0f, 3.0f, 2.0f, ParamUnit::none, 0.0f, "Osc"));
    p.push_back (P::choice ("osc1Octave", "Osc 1 Octave", synth::octaveNames(), 2, "Osc"));
    p.push_back (P::continuous ("osc2Shape", "Osc 2 Shape", 0.0f, 3.0f, 2.0f, ParamUnit::none, 0.0f, "Osc"));
    p.push_back (P::choice ("osc2Octave", "Osc 2 Octave", synth::octaveNames(), 2, "Osc"));
    p.push_back (P::continuous ("osc2Semi", "Osc 2 Semi", -12.0f, 12.0f, 0.0f, ParamUnit::semitones, 0.0f, "Osc"));
    p.push_back (P::continuous ("osc2Detune", "Osc 2 Detune", -50.0f, 50.0f, 8.0f, ParamUnit::cents, 0.0f, "Osc"));
    p.push_back (P::continuous ("oscMix", "Osc Mix", 0.0f, 1.0f, 0.5f, ParamUnit::percent, 0.0f, "Osc"));
    p.push_back (P::continuous ("pulseWidth", "Pulse Width", 0.05f, 0.95f, 0.5f, ParamUnit::percent, 0.0f, "Osc"));
    p.push_back (P::continuous ("subLevel", "Sub", 0.0f, 1.0f, 0.0f, ParamUnit::percent, 0.0f, "Osc"));
    p.push_back (P::continuous ("noiseLevel", "Noise", 0.0f, 1.0f, 0.0f, ParamUnit::percent, 0.0f, "Osc"));
    p.push_back (P::continuous ("fmAmount", "FM 2 > 1", 0.0f, 1.0f, 0.0f, ParamUnit::percent, 0.0f, "Osc"));
    // Filter
    p.push_back (P::choice ("filterType", "Filter Type", synth::filterTypeNames(), synth::filterLp24, "Filter"));
    p.push_back (P::continuous ("cutoff", "Cutoff", 20.0f, 20000.0f, 1800.0f, ParamUnit::hertz, 1000.0f, "Filter"));
    p.push_back (P::continuous ("resonance", "Resonance", 0.0f, 1.0f, 0.2f, ParamUnit::percent, 0.0f, "Filter"));
    p.push_back (P::continuous ("filterEnv", "Env Amount", -1.0f, 1.0f, 0.35f, ParamUnit::percent, 0.0f, "Filter"));
    p.push_back (P::continuous ("keyTrack", "Key Track", 0.0f, 1.0f, 0.5f, ParamUnit::percent, 0.0f, "Filter"));
    p.push_back (P::continuous ("drive", "Drive", 0.0f, 1.0f, 0.0f, ParamUnit::percent, 0.0f, "Filter"));
    // Env
    p.push_back (P::continuous ("ampAttack", "Attack", 0.001f, 10.0f, 0.003f, ParamUnit::seconds, 0.4f, "Env"));
    p.push_back (P::continuous ("ampDecay", "Decay", 0.001f, 10.0f, 0.8f, ParamUnit::seconds, 0.6f, "Env"));
    p.push_back (P::continuous ("ampSustain", "Sustain", 0.0f, 1.0f, 0.7f, ParamUnit::percent, 0.0f, "Env"));
    p.push_back (P::continuous ("ampRelease", "Release", 0.001f, 15.0f, 0.3f, ParamUnit::seconds, 0.8f, "Env"));
    p.push_back (P::continuous ("modAttack", "Mod Attack", 0.001f, 10.0f, 0.002f, ParamUnit::seconds, 0.4f, "Env"));
    p.push_back (P::continuous ("modDecay", "Mod Decay", 0.001f, 10.0f, 0.5f, ParamUnit::seconds, 0.6f, "Env"));
    p.push_back (P::continuous ("modSustain", "Mod Sustain", 0.0f, 1.0f, 0.2f, ParamUnit::percent, 0.0f, "Env"));
    p.push_back (P::continuous ("modRelease", "Mod Release", 0.001f, 15.0f, 0.3f, ParamUnit::seconds, 0.8f, "Env"));
    // LFO
    p.push_back (P::choice ("lfoShape", "LFO Shape", synth::lfoShapeNames(), 0, "LFO"));
    p.push_back (P::continuous ("lfoRate", "LFO Rate", 0.05f, 30.0f, 3.0f, ParamUnit::hertz, 3.0f, "LFO"));
    p.push_back (P::toggle ("lfoSync", "LFO Sync", false, "LFO"));
    p.push_back (P::choice ("lfoDivision", "LFO Division", synth::lfoDivisionNames(), 5, "LFO"));
    p.push_back (P::continuous ("lfoAmount", "LFO Amount", 0.0f, 1.0f, 0.0f, ParamUnit::percent, 0.0f, "LFO"));
    p.push_back (P::choice ("lfoDest", "LFO Target", { "Pitch", "Filter", "Amp", "Pulse Width", "Shape", "Osc Mix" },
                            analog::destFilter, "LFO"));
    p.push_back (P::continuous ("lfoFade", "LFO Fade In", 0.0f, 5.0f, 0.0f, ParamUnit::seconds, 1.0f, "LFO"));
    p.push_back (P::toggle ("lfoRetrig", "LFO Retrigger", false, "LFO"));
    // Mod
    p.push_back (P::continuous ("velToFilter", "Vel > Filter", 0.0f, 1.0f, 0.3f, ParamUnit::percent, 0.0f, "Mod"));
    p.push_back (P::continuous ("velToAmp", "Vel > Amp", 0.0f, 1.0f, 0.5f, ParamUnit::percent, 0.0f, "Mod"));
    p.push_back (P::continuous ("modEnvPitch", "Mod Env > Pitch", -24.0f, 24.0f, 0.0f, ParamUnit::semitones, 0.0f, "Mod"));
    p.push_back (P::continuous ("modEnvOsc2", "Mod Env > Osc 2", -24.0f, 24.0f, 0.0f, ParamUnit::semitones, 0.0f, "Mod"));
    p.push_back (P::continuous ("modEnvFm", "Mod Env > FM", 0.0f, 1.0f, 0.0f, ParamUnit::percent, 0.0f, "Mod"));
    // Global
    p.push_back (P::choice ("voiceMode", "Voices", synth::voiceModeNames(), 0, "Global"));
    p.push_back (P::continuous ("glide", "Glide", 0.0f, 2.0f, 0.0f, ParamUnit::seconds, 0.3f, "Global"));
    p.push_back (P::continuous ("drift", "Vintage", 0.0f, 1.0f, 0.25f, ParamUnit::percent, 0.0f, "Global"));
    p.push_back (P::continuous ("spread", "Spread", 0.0f, 1.0f, 0.3f, ParamUnit::percent, 0.0f, "Global"));
    p.push_back (P::continuous ("transpose", "Transpose", -24.0f, 24.0f, 0.0f, ParamUnit::semitones, 0.0f, "Global"));
    p.push_back (P::continuous ("volume", "Volume", -70.0f, 6.0f, 0.0f, ParamUnit::decibels, 0.0f, "Global"));

    jassert ((int) p.size() == analog::numParams);
    return spec;
}

//==============================================================================
struct AnalogSynth::Impl
{
    double sampleRate = 48000.0;
    synth::ParamSmoother params;
    synth::VoiceAllocator<analog::Voice> voices;
    analog::Control ctl;
    float lastVolume = -1.0f;

    Impl()
    {
        params.configure (AnalogSynth::createSpec());
        prepare (48000.0);
    }

    void prepare (double sr)
    {
        sampleRate = sr;
        params.prepare (sr);
        uint32_t seed = 0x1234567u;
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
    }

    void updateControl (int len)
    {
        using namespace analog;
        params.advance (len);
        const auto sr = (float) sampleRate;
        auto& c = ctl;

        const float trans = std::round (params[transpose]);
        c.shape1 = params[osc1Shape];
        c.shape2 = params[osc2Shape];
        c.osc1Semis = (float) (params.getChoice (osc1Octave) - 2) * 12.0f + trans;
        c.osc2Semis = (float) (params.getChoice (osc2Octave) - 2) * 12.0f + std::round (params[osc2Semi])
                      + params[osc2Detune] * 0.01f + trans;
        c.mix = params[oscMix];
        c.pulseWidth = params[pulseWidth];
        c.sub = params[subLevel] * 0.8f;
        c.noise = params[noiseLevel] * 0.7f;
        c.fm = params[fmAmount];
        c.fmEnv = params[modEnvFm];

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
        c.lfoIncrement = params.getChoice (lfoSync) != 0
                             ? (float) (c.tempo / 60.0 / synth::lfoDivisionBeats (params.getChoice (lfoDivision))) / sr
                             : params[lfoRate] / sr;
        c.advanceBend (len, sr);
        const float fade = params[lfoFade];
        c.lfoFadeIncrement = fade > 0.001f ? 1.0f / (fade * sr) : 1.0f;
        c.lfoRetrigger = params.getChoice (lfoRetrig) != 0;
        c.globalLfoPhase += c.lfoIncrement * (float) len;
        c.globalLfoPhase -= std::floor (c.globalLfoPhase);

        c.velToFilter = params[velToFilter];
        c.velToAmp = params[velToAmp];
        c.modEnvPitch = params[modEnvPitch];
        c.modEnvOsc2 = params[modEnvOsc2];

        const float glideTime = params[glide];
        c.glideOn = glideTime > 0.0005f;
        c.glideCoeff = c.glideOn ? 1.0f - std::exp (-3.0f * (float) len / (glideTime * sr)) : 1.0f;

        c.drift = params[drift];
        c.spread = voices.getMode() == synth::VoiceMode::poly ? params[spread] : 0.0f;
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

            const float vol = params[analog::volume];
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
        voices.setMode ((synth::VoiceMode) juce::jlimit (0, 2, params.getChoice (analog::voiceMode)));
        updateControl (0); // notes at offset 0 start with this block's settings

        float* left = dry.getWritePointer (0);
        float* right = dry.getNumChannels() > 1 ? dry.getWritePointer (1) : nullptr;

        dsp::renderWithEvents (events, numEvents, numSamples,
            [this] (const NoteEvent& e)
            {
                const int pitch = juce::jlimit (0, 127, e.pitch);
                if (e.isNoteOn())
                {
                    ctl.notePressure[(size_t) pitch] = 0.0f; // a new note starts without poly pressure
                    voices.noteOn (pitch, juce::jlimit (0.0f, 1.0f, e.velocity));
                }
                else
                    voices.noteOff (pitch);
            },
            [&] (int start, int count) { renderRange (left + start, right != nullptr ? right + start : nullptr, count); });
    }
};

AnalogSynth::AnalogSynth() : impl (std::make_unique<Impl>()) {}
AnalogSynth::~AnalogSynth() = default;

void AnalogSynth::prepare (double sampleRate, int) { impl->prepare (sampleRate); }
void AnalogSynth::reset() { impl->reset(); }
void AnalogSynth::setParameters (const float* values, int numValues) { impl->params.setTargets (values, numValues); }
void AnalogSynth::allNotesOff() { impl->voices.killAll(); }
void AnalogSynth::setPitchBend (float semitones) { impl->ctl.bendTarget = semitones; }
void AnalogSynth::setTempo (double bpm) { impl->ctl.tempo = bpm; }

void AnalogSynth::setPressure (int note, float pressure)
{
    pressure = juce::jlimit (0.0f, 1.0f, pressure);
    if (note < 0)
        impl->ctl.channelPressure = pressure;
    else if (note < 128)
        impl->ctl.notePressure[(size_t) note] = pressure;
}

void AnalogSynth::process (juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>&, const NoteEvent* events,
                           int numEvents, int numSamples)
{
    impl->process (dry, events, numEvents, numSamples);
}

} // namespace rb
