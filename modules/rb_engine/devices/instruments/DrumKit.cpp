namespace rb
{

DeviceSpec DrumKit::createSpec()
{
    DeviceSpec spec;
    spec.typeId = "drumKit";
    spec.displayName = "Drum Kit";
    spec.params.push_back (ParamSpec::continuous ("volume", "Volume", -70.0f, 6.0f, 0.0f, ParamUnit::decibels, 0.0f, "Kit"));
    spec.groups = { "Kit" };
    return spec;
}

DeviceSpec DrumKit::createPadSpec()
{
    const juce::String page1 = "Page 1", page2 = "Page 2", padFx = "Pad FX", sample = "Sample";

    DeviceSpec spec;
    spec.typeId = "pad";
    spec.displayName = "Pad";
    auto& p = spec.params;
    p.push_back (ParamSpec::continuous ("transpose", "Transpose", -48.0f, 48.0f, 0.0f, ParamUnit::semitones, 0.0f, page1));
    p.push_back (ParamSpec::choice ("filterType", "Filter", { "LP12", "LP24", "HP24", "Peak" }, 0, page1));
    p.push_back (ParamSpec::continuous ("cutoff", "Cutoff", 20.0f, 20000.0f, 20000.0f, ParamUnit::hertz, 1000.0f, page1));
    p.push_back (ParamSpec::continuous ("resonance", "Resonance", 0.0f, 1.0f, 0.1f, ParamUnit::percent, 0.0f, page1));
    p.push_back (ParamSpec::continuous ("detune", "Detune", -50.0f, 50.0f, 0.0f, ParamUnit::cents, 0.0f, page1));
    p.push_back (ParamSpec::continuous ("attack", "Attack", 0.0f, 2.0f, 0.0f, ParamUnit::seconds, 0.1f, page1));
    p.push_back (ParamSpec::continuous ("hold", "Hold", 0.0f, 10.0f, 10.0f, ParamUnit::seconds, 0.5f, page1));
    p.push_back (ParamSpec::choice ("envMode", "Mode", { "Trig", "Gate" }, 0, page1));
    p.push_back (ParamSpec::continuous ("decay", "Decay", 0.01f, 10.0f, 10.0f, ParamUnit::seconds, 0.5f, page1));
    p.push_back (ParamSpec::continuous ("pan", "Pan", -1.0f, 1.0f, 0.0f, ParamUnit::none, 0.0f, page2));
    p.push_back (ParamSpec::choice ("fxType", "FX Type",
                                    { "None", "Stretch", "Loop", "Pitch Env", "Punch", "8-Bit", "FM", "Ring Mod", "Sub Osc", "Noise" },
                                    0, page2));
    p.push_back (ParamSpec::continuous ("fx1", "FX 1", 0.0f, 1.0f, 0.5f, ParamUnit::percent, 0.0f, page2));
    p.push_back (ParamSpec::continuous ("fx2", "FX 2", 0.0f, 1.0f, 0.5f, ParamUnit::percent, 0.0f, page2));
    p.push_back (ParamSpec::choice ("velTarget", "Vel Target", { "Filter", "Attack", "Hold", "Decay", "FX 1", "FX 2" }, 0, page2));
    p.push_back (ParamSpec::continuous ("velAmount", "Vel Amount", -1.0f, 1.0f, 0.0f, ParamUnit::percent, 0.0f, page2));
    p.push_back (ParamSpec::continuous ("velVolume", "Vel > Vol", 0.0f, 1.0f, 0.7f, ParamUnit::percent, 0.0f, page2));
    p.push_back (ParamSpec::continuous ("send", "Send", 0.0f, 1.0f, 0.0f, ParamUnit::percent, 0.0f, padFx));
    p.push_back (ParamSpec::toggle ("choke", "Choke", false, sample));
    p.push_back (ParamSpec::continuous ("gain", "Gain", -70.0f, 24.0f, 0.0f, ParamUnit::decibels, 0.0f, sample));
    p.push_back (ParamSpec::continuous ("sampleStart", "Start", 0.0f, 1.0f, 0.0f, ParamUnit::percent, 0.0f, sample));
    spec.groups = { page1, page2, padFx, sample };
    return spec;
}

//==============================================================================
// Values a voice needs for one chunk (<= chunkSize samples), derived from its pad's current
// parameters and its velocity. Recomputed every chunk so knob moves affect ringing hits.
struct DrumKit::ChunkParams
{
    double increment = 1.0;   // sample frames per output sample
    int fxType = fxNone;
    float fx1 = 0.5f, fx2 = 0.5f;

    float attackSamples = 1.0f;
    float holdSamples = -1.0f;  // < 0: infinite
    float decaySamples = -1.0f; // < 0: infinite

    int filterType = filterLp12;
    float cutoffHz = 20000.0f, resonance = 0.1f;

    float targetL = 1.0f, targetR = 1.0f, targetSend = 0.0f;

    // Playback FX (only the fields of the active type are set)
    float stretch = 1.0f, grainSamples = 1000.0f;
    double loopStart = 0.0, loopLength = 0.0, loopFade = 0.0;
    float fmDepth = 0.0f, oscIncrement = 0.0f, mix = 0.0f;
    float envDecay = 1.0f, amount = 0.0f;
    float crushStep = 0.0f, crushRate = 1.0f;
    float noiseCoeff = 1.0f;
};

namespace drumkit
{
    constexpr int chunkSize = 16;
    constexpr float infiniteTime = 9.99f; // hold/decay at (or near) their maximum never end

    static float clamp01 (float x) { return juce::jlimit (0.0f, 1.0f, x); }
    static float wrapPhase (float p) { return p >= dsp::twoPi ? p - dsp::twoPi : p; }

    // Rational tanh approximation (exact at 0, saturates at +-1), much cheaper than std::tanh.
    static float softClip (float x)
    {
        if (x <= -3.0f) return -1.0f;
        if (x >= 3.0f) return 1.0f;
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    // sin(pi * phase) for phase in 0..1 (Bhaskara I, error < 0.2 %).
    static float halfSine (float phase)
    {
        const float p = phase * (1.0f - phase);
        return 16.0f * p / (5.0f - 4.0f * p);
    }
} // namespace drumkit


DrumKit::DrumKit()
{
    const auto defaults = createPadSpec().defaults();
    jassert ((int) defaults.size() == numPadParams);
    for (auto& pad : padValues)
        for (size_t i = 0; i < pad.size() && i < defaults.size(); ++i)
            pad[i] = defaults[i];
}

void DrumKit::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    volume.prepare (sampleRate, 0.02f);
    volume.snap (dsp::dbToGain (volumeDb));
    declicker.prepare (sampleRate);
    for (auto& v : voices)
        for (auto& f : v.filters)
            f.prepare (sampleRate);
    reset();
}

void DrumKit::reset()
{
    for (auto& v : voices)
        v.active = false;
    declicker.reset();
}

void DrumKit::setParameters (const float* values, int numValues)
{
    if (numValues > 0)
    {
        volumeDb = values[0];
        volume.setTarget (dsp::dbToGain (volumeDb));
    }
}

void DrumKit::setPadParameters (int pad, const float* values, int numValues)
{
    if (! juce::isPositiveAndBelow (pad, numDrumPads) || values == nullptr)
        return;
    auto& dest = padValues[(size_t) pad];
    const int n = juce::jmin (numValues, (int) numPadParams);
    for (int i = 0; i < n; ++i)
        dest[(size_t) i] = values[i];
}

void DrumKit::setSample (int pad, const SampleData* sample)
{
    if (! juce::isPositiveAndBelow (pad, numDrumPads) || samples[(size_t) pad] == sample)
        return;

    // The old pointer may be freed after this call: stop every hit still reading it.
    for (auto& v : voices)
        if (v.active && v.pad == pad)
            killVoice (v);

    samples[(size_t) pad] = sample;
}

void DrumKit::allNotesOff()
{
    for (auto& v : voices)
        if (v.active)
            killVoice (v);
}

int DrumKit::getNumActiveVoices() const
{
    int n = 0;
    for (auto& v : voices)
        n += v.active ? 1 : 0;
    return n;
}

void DrumKit::killVoice (Voice& v)
{
    declicker.add (v.lastL, v.lastR, v.lastSendL, v.lastSendR);
    v.active = false;
    v.sample = nullptr;
}

void DrumKit::beginFade (Voice& v, float seconds)
{
    const float step = 1.0f / juce::jmax (1.0f, seconds * (float) sampleRate);
    if (v.fading)
    {
        v.fadeStep = juce::jmax (v.fadeStep, step);
        return;
    }
    v.fading = true;
    v.fadeStep = step;
}

DrumKit::Voice* DrumKit::allocateVoice()
{
    int sounding = 0;
    Voice* oldest = nullptr;
    for (auto& v : voices)
    {
        if (! v.active || v.fading)
            continue;
        ++sounding;
        if (oldest == nullptr || v.age < oldest->age)
            oldest = &v;
    }

    if (sounding >= maxVoices && oldest != nullptr)
        beginFade (*oldest, 0.003f);

    for (auto& v : voices)
        if (! v.active)
            return &v;

    // Every slot is busy (many hits fading): cut the quietest fading hit, or the oldest.
    Voice* victim = nullptr;
    for (auto& v : voices)
        if (v.fading && (victim == nullptr || v.fadeGain < victim->fadeGain))
            victim = &v;
    if (victim == nullptr)
        victim = oldest != nullptr ? oldest : &voices[0];
    killVoice (*victim);
    return victim;
}

void DrumKit::handleEvent (const NoteEvent& e)
{
    const int pad = e.pitch - drumBaseNote;
    if (! juce::isPositiveAndBelow (pad, numDrumPads))
        return;

    if (e.isNoteOn())
    {
        startVoice (pad, e);
        return;
    }

    // Note-off: Gate-mode hits of this pad release quickly; Trig-mode hits ignore it.
    for (auto& v : voices)
    {
        if (! v.active || v.pad != pad || ! v.gateMode)
            continue;
        if (v.stage == Voice::Stage::release || v.stage == Voice::Stage::done)
            continue;
        v.stage = Voice::Stage::release;
        v.releaseStep = juce::jmax (v.envLevel, 1.0e-4f) / (0.01f * (float) sampleRate);
    }
}

void DrumKit::startVoice (int pad, const NoteEvent& e)
{
    const auto* sample = samples[(size_t) pad];
    if (sample == nullptr || sample->getNumFrames() <= 0 || sample->buffer.getNumChannels() <= 0)
        return;

    const auto& p = padValues[(size_t) pad];

    // One choke group per kit: a choking pad silences every other sounding choking hit.
    if (p[pChoke] > 0.5f)
        for (auto& v : voices)
            if (v.active && ! v.fading && padValues[(size_t) v.pad][pChoke] > 0.5f)
                beginFade (v, 0.006f);

    Voice* v = allocateVoice();
    *v = Voice();
    for (auto& f : v->filters)
        f.prepare (sampleRate);

    v->active = true;
    v->pad = pad;
    v->sample = sample;
    v->age = nextAge++;
    v->velocity = drumkit::clamp01 (e.velocity);
    v->velocityCurve = std::pow (v->velocity, 1.5f);
    v->pitchOffset = e.pitchOffset;
    v->rateRatio = sample->sampleRate / sampleRate;
    v->startFrame = (double) drumkit::clamp01 (p[pSampleStart]) * (double) (sample->getNumFrames() - 1);
    v->position = v->stretchPosition = v->startFrame;
    v->grainHead = { v->startFrame, v->startFrame };
    v->grainPhase = { 0.0f, 0.5f };
    v->gateMode = juce::roundToInt (p[pEnvMode]) == 1;
    noiseSeed = noiseSeed * 1664525u + 1013904223u;
    v->noise.state = noiseSeed | 1u;

    const auto c = computeChunkParams (*v);
    v->logCutoff = std::log2 (c.cutoffHz);
    v->peakGain = dsp::dbToGain (c.resonance * 24.0f);
    v->gainL = c.targetL;
    v->gainR = c.targetR;
    v->sendGain = c.targetSend;
}

DrumKit::ChunkParams DrumKit::computeChunkParams (const Voice& v) const
{
    const auto& p = padValues[(size_t) v.pad];
    const float sr = (float) sampleRate;
    ChunkParams c;

    const float velMod = juce::jlimit (-1.0f, 1.0f, p[pVelAmount] * v.velocity);
    const int velTarget = juce::roundToInt (p[pVelTarget]);
    auto modFor = [velMod, velTarget] (int target) { return velTarget == target ? velMod : 0.0f; };

    c.fxType = juce::roundToInt (p[pFxType]);
    c.fx1 = drumkit::clamp01 (p[pFx1] + modFor (velFx1));
    c.fx2 = drumkit::clamp01 (p[pFx2] + modFor (velFx2));

    // Pitch
    float semitones = p[pTranspose] + p[pDetune] * 0.01f + v.pitchOffset;
    if (c.fxType == fxPitchEnv)
    {
        // FX 1: depth, bipolar around the centre (+-36 st); FX 2: decay 5 ms .. 1.3 s.
        const float depth = (c.fx1 - 0.5f) * 72.0f;
        const float tau = 0.005f * std::exp2 (c.fx2 * 8.0f);
        semitones += depth * std::exp (-(float) v.elapsed / (tau * sr));
    }
    const float pitchRatio = std::exp2 (semitones / 12.0f);
    c.increment = v.rateRatio * (double) pitchRatio;

    // AHD envelope; velocity can shorten or lengthen the stages.
    const float attack = juce::jlimit (0.0002f, 2.0f, p[pAttack] + modFor (velAttack));
    const float hold = p[pHold] * std::exp2 (modFor (velHold) * 4.0f);
    const float decay = p[pDecay] * std::exp2 (modFor (velDecay) * 4.0f);
    // At their maximum, hold and decay last as long as the sample, except in Loop mode where
    // the sample never runs out.
    const bool sampleCanEnd = c.fxType != fxLoop;
    c.attackSamples = attack * sr;
    c.holdSamples = (sampleCanEnd && hold >= drumkit::infiniteTime) ? -1.0f : hold * sr;
    c.decaySamples = (sampleCanEnd && decay >= drumkit::infiniteTime) ? -1.0f : juce::jmax (0.005f, decay) * sr;

    // Filter; velocity sweeps the cutoff by up to +-6 octaves.
    c.filterType = juce::roundToInt (p[pFilterType]);
    c.cutoffHz = juce::jlimit (20.0f, 20000.0f, p[pCutoff] * std::exp2 (modFor (velFilter) * 6.0f));
    c.resonance = drumkit::clamp01 (p[pResonance]);

    // Level: sample gain x velocity curve, then pan.
    const float velVolume = drumkit::clamp01 (p[pVelVolume]);
    const float amp = dsp::dbToGain (p[pGain]) * ((1.0f - velVolume) + velVolume * v.velocityCurve);
    float panL = 1.0f, panR = 1.0f;
    dsp::panGains (p[pPan], panL, panR);
    c.targetL = amp * panL;
    c.targetR = amp * panR;
    c.targetSend = drumkit::clamp01 (p[pSend]);

    const double sampleFrameRate = v.sample != nullptr ? v.sample->sampleRate : sampleRate;
    switch (c.fxType)
    {
        case fxStretch:
            // FX 1: stretch factor 1x .. 8x; FX 2: grain size 10 .. 200 ms.
            c.stretch = std::exp2 (c.fx1 * 3.0f);
            c.grainSamples = sr * 0.01f * std::exp2 (c.fx2 * 4.32f);
            break;

        case fxLoop:
        {
            // FX 1: loop length 5 .. 500 ms; FX 2: loop position within the played region.
            const double region = juce::jmax (1.0, (double) (v.sample != nullptr ? v.sample->getNumFrames() : 1) - v.startFrame);
            c.loopLength = juce::jlimit (1.0, region, sampleFrameRate * 0.005 * std::exp2 ((double) c.fx1 * 6.64));
            c.loopStart = v.startFrame + (double) c.fx2 * (region - c.loopLength);
            c.loopFade = juce::jmin (c.loopLength * 0.25, sampleFrameRate * 0.004);
            break;
        }

        case fxPunch:
            // FX 1: transient boost up to +14 dB (soft-clipped); FX 2: transient time 5 .. 160 ms.
            c.amount = c.fx1 * 4.0f;
            c.envDecay = std::exp (-1.0f / (0.005f * std::exp2 (c.fx2 * 5.0f) * sr));
            break;

        case fxBitCrush:
            // FX 1: bit depth 16 .. 1 bits; FX 2: sample-rate reduction 1x .. 32x.
            c.crushStep = std::exp2 (1.0f - (16.0f - 15.0f * c.fx1));
            c.crushRate = std::exp2 (-c.fx2 * 5.0f);
            break;

        case fxFm:
            // FX 1: modulation depth; FX 2: modulator frequency 30 Hz .. 3.8 kHz (follows pitch).
            c.fmDepth = c.fx1 * 0.9f;
            c.oscIncrement = dsp::twoPi * juce::jmin (0.45f * sr, 30.0f * std::exp2 (c.fx2 * 7.0f) * pitchRatio) / sr;
            break;

        case fxRingMod:
            // FX 1: carrier 20 Hz .. 5 kHz; FX 2: dry/wet.
            c.oscIncrement = dsp::twoPi * 20.0f * std::exp2 (c.fx1 * 8.0f) / sr;
            c.mix = c.fx2;
            break;

        case fxSubOsc:
            // FX 1: sub level; FX 2: sub pitch 30 .. 150 Hz (follows transpose). Decays over ~1 s.
            c.amount = c.fx1;
            c.oscIncrement = dsp::twoPi * juce::jmin (0.45f * sr, 30.0f * std::exp2 (c.fx2 * 2.32f) * pitchRatio) / sr;
            c.envDecay = std::exp (-1.0f / (0.35f * sr));
            break;

        case fxNoise:
            // FX 1: noise level (follows the sample's loudness); FX 2: colour, dark .. bright.
            c.amount = c.fx1 * 2.0f;
            c.noiseCoeff = 1.0f - std::exp (-dsp::twoPi * juce::jmin (0.45f * sr, 300.0f * std::exp2 (c.fx2 * 6.0f)) / sr);
            break;

        default:
            break;
    }

    return c;
}

void DrumKit::process (juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>& send,
                       const NoteEvent* events, int numEvents, int numSamples)
{
    float* dl = dry.getWritePointer (0);
    float* dr = dry.getWritePointer (dry.getNumChannels() > 1 ? 1 : 0);
    float* sl = send.getWritePointer (0);
    float* sr = send.getWritePointer (send.getNumChannels() > 1 ? 1 : 0);

    dsp::renderWithEvents (events, numEvents, numSamples,
                           [this] (const NoteEvent& e) { handleEvent (e); },
                           [&] (int start, int count) { renderRange (dl, dr, sl, sr, start, count); });

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = volume.next();
        dl[i] *= g;
        dr[i] *= g;
        sl[i] *= g;
        sr[i] *= g;
    }
}

void DrumKit::renderRange (float* dl, float* dr, float* sl, float* sr, int start, int count)
{
    for (int done = 0; done < count;)
    {
        const int n = juce::jmin (drumkit::chunkSize, count - done);
        const int offset = start + done;

        for (auto& v : voices)
            if (v.active)
                renderVoice (v, dl + offset, dr + offset, sl + offset, sr + offset, n);

        declicker.render (dl + offset, dr + offset, sl + offset, sr + offset, n);
        done += n;
    }
}

void DrumKit::renderVoice (Voice& v, float* dl, float* dr, float* sl, float* sr, int count)
{
    const auto c = computeChunkParams (v);
    const auto& sample = *v.sample;
    const double frames = (double) sample.getNumFrames();
    const bool stereo = sample.buffer.getNumChannels() > 1;
    const float sampleRateF = (float) sampleRate;

    // Filter coefficients (cutoff smoothed in the log domain).
    v.logCutoff += (std::log2 (c.cutoffHz) - v.logCutoff) * 0.25f;
    const float cutoff = std::exp2 (v.logCutoff);
    switch (c.filterType)
    {
        case filterLp24:
        case filterHp24:
            v.filters[0].set (cutoff, c.resonance * 0.85f);
            v.filters[1].set (cutoff, c.resonance * 0.4f);
            if (stereo)
            {
                v.filters[2].set (cutoff, c.resonance * 0.85f);
                v.filters[3].set (cutoff, c.resonance * 0.4f);
            }
            break;
        case filterPeak:
            // Bell at the cutoff; resonance sets the boost (0 .. +24 dB).
            v.filters[0].set (cutoff, 0.5f);
            if (stereo)
                v.filters[2].set (cutoff, 0.5f);
            v.peakGain += (dsp::dbToGain (c.resonance * 24.0f) - v.peakGain) * 0.25f;
            break;
        default:
            v.filters[0].set (cutoff, c.resonance);
            if (stereo)
                v.filters[2].set (cutoff, c.resonance);
            break;
    }

    auto filter = [&c, &v] (float x, int first) -> float
    {
        auto& a = v.filters[(size_t) first];
        auto& b = v.filters[(size_t) first + 1];
        switch (c.filterType)
        {
            case filterLp24: return b.tick (a.tick (x).low).low;
            case filterHp24: return b.tick (a.tick (x).high).high;
            case filterPeak: return x + (v.peakGain - 1.0f) * a.getK() * a.tick (x).band;
            default:         return a.tick (x).low;
        }
    };

    const float invCount = 1.0f / (float) count;
    const float stepL = (c.targetL - v.gainL) * invCount;
    const float stepR = (c.targetR - v.gainR) * invCount;
    const float stepSend = (c.targetSend - v.sendGain) * invCount;
    const double endFrame = frames + 4.0; // the interpolator reads silence beyond the data

    bool finished = false;

    for (int i = 0; i < count; ++i)
    {
        // 1. Read the sample (Stretch, Loop and FM change how the playhead moves).
        sampler::Frame x;
        bool exhausted = false;

        switch (c.fxType)
        {
            case fxStretch:
            {
                // Two overlapping Hann grains read at the pitch rate from a position that
                // advances 1/stretch as fast.
                for (size_t g = 0; g < 2; ++g)
                {
                    v.grainPhase[g] += 1.0f / c.grainSamples;
                    if (v.grainPhase[g] >= 1.0f)
                    {
                        v.grainPhase[g] -= 1.0f;
                        v.grainHead[g] = v.stretchPosition;
                    }
                    const float s = drumkit::halfSine (v.grainPhase[g]);
                    const auto f = sampler::readFrame (sample, v.grainHead[g]);
                    x.l += f.l * s * s;
                    x.r += f.r * s * s;
                    v.grainHead[g] += c.increment;
                }
                const float fade = sampler::endFade (sample, v.stretchPosition);
                x.l *= fade;
                x.r *= fade;
                v.stretchPosition += c.increment / (double) c.stretch;
                exhausted = v.stretchPosition >= frames;
                break;
            }

            case fxLoop:
            {
                const double loopEnd = c.loopStart + c.loopLength;
                if (v.position >= loopEnd)
                    v.position = c.loopStart + std::fmod (v.position - loopEnd, c.loopLength);

                x = sampler::readFrame (sample, v.position);
                const double fadeStart = loopEnd - c.loopFade;
                if (c.loopFade > 0.0 && v.position > fadeStart)
                {
                    // Crossfade into the material just before the loop start.
                    const float a = (float) ((v.position - fadeStart) / c.loopFade);
                    const auto y = sampler::readFrame (sample, v.position - c.loopLength);
                    x.l += (y.l - x.l) * a;
                    x.r += (y.r - x.r) * a;
                }
                v.position += c.increment;
                break;
            }

            case fxFm:
            {
                const float fade = sampler::endFade (sample, v.position);
                x = sampler::readFrame (sample, v.position);
                x.l *= fade;
                x.r *= fade;
                v.position += c.increment * (double) (1.0f + c.fmDepth * std::sin (v.fxPhase));
                v.fxPhase = drumkit::wrapPhase (v.fxPhase + c.oscIncrement);
                exhausted = v.position >= frames;
                break;
            }

            default:
            {
                const float fade = sampler::endFade (sample, v.position);
                x = sampler::readFrame (sample, v.position);
                x.l *= fade;
                x.r *= fade;
                v.position += c.increment;
                exhausted = v.position >= frames;
                break;
            }
        }

        // 2. Effects that shape the read signal.
        switch (c.fxType)
        {
            case fxPunch:
            {
                const float g = 1.0f + c.amount * v.fxEnv;
                v.fxEnv *= c.envDecay;
                x.l = drumkit::softClip (x.l * g);
                x.r = drumkit::softClip (x.r * g);
                break;
            }

            case fxBitCrush:
                v.crushPhase += c.crushRate;
                if (v.crushPhase >= 1.0f)
                {
                    v.crushPhase -= 1.0f;
                    v.crushHeld.l = std::round (x.l / c.crushStep) * c.crushStep;
                    v.crushHeld.r = std::round (x.r / c.crushStep) * c.crushStep;
                }
                x = v.crushHeld;
                break;

            case fxRingMod:
            {
                const float m = 1.0f - c.mix + c.mix * std::sin (v.fxPhase);
                v.fxPhase = drumkit::wrapPhase (v.fxPhase + c.oscIncrement);
                x.l *= m;
                x.r *= m;
                break;
            }

            case fxSubOsc:
            {
                const float s = std::sin (v.fxPhase) * c.amount * v.fxEnv;
                v.fxPhase = drumkit::wrapPhase (v.fxPhase + c.oscIncrement);
                v.fxEnv *= c.envDecay;
                x.l += s;
                x.r += s;
                break;
            }

            case fxNoise:
            {
                const float level = 0.5f * (std::abs (x.l) + std::abs (x.r));
                v.follower += (level - v.follower) * (level > v.follower ? 0.02f : 0.0003f);
                v.noiseLp += (v.noise.next() - v.noiseLp) * c.noiseCoeff;
                const float s = v.noiseLp * v.follower * c.amount;
                x.l += s;
                x.r += s;
                break;
            }

            default:
                break;
        }

        // 3. Filter (mono samples run one channel and copy it).
        x.l = filter (x.l, 0);
        x.r = stereo ? filter (x.r, 2) : x.l;

        // 4. AHD envelope.
        switch (v.stage)
        {
            case Voice::Stage::attack:
                v.envPhase += 1.0f / c.attackSamples;
                if (v.envPhase >= 1.0f)
                {
                    v.envLevel = 1.0f;
                    v.envPhase = 0.0f;
                    v.stage = v.gateMode ? Voice::Stage::decay : Voice::Stage::hold;
                }
                else
                {
                    v.envLevel = v.envPhase;
                }
                break;

            case Voice::Stage::hold:
                v.envLevel = 1.0f;
                if (c.holdSamples >= 0.0f)
                {
                    v.envPhase += 1.0f;
                    if (v.envPhase >= c.holdSamples)
                    {
                        v.envPhase = 0.0f;
                        v.stage = Voice::Stage::decay;
                    }
                }
                break;

            case Voice::Stage::decay:
                if (c.decaySamples > 0.0f)
                    v.envPhase += 1.0f / c.decaySamples;
                if (v.envPhase >= 1.0f)
                {
                    v.envLevel = 0.0f;
                    v.stage = Voice::Stage::done;
                }
                else
                {
                    const float r = 1.0f - v.envPhase;
                    v.envLevel = r * r * r;
                }
                break;

            case Voice::Stage::release:
                v.envLevel -= v.releaseStep;
                if (v.envLevel <= 0.0f)
                {
                    v.envLevel = 0.0f;
                    v.stage = Voice::Stage::done;
                }
                break;

            case Voice::Stage::done:
                v.envLevel = 0.0f;
                break;
        }

        float gain = v.envLevel;
        if (v.fading)
        {
            v.fadeGain -= v.fadeStep;
            if (v.fadeGain <= 0.0f)
            {
                v.fadeGain = 0.0f;
                finished = true;
            }
            gain *= v.fadeGain;
        }

        // 5. Pan and output.
        v.gainL += stepL;
        v.gainR += stepR;
        v.sendGain += stepSend;
        const float outL = x.l * gain * v.gainL;
        const float outR = x.r * gain * v.gainR;
        dl[i] += outL;
        dr[i] += outR;
        sl[i] += outL * v.sendGain;
        sr[i] += outR * v.sendGain;
        v.lastL = outL;
        v.lastR = outR;
        v.lastSendL = outL * v.sendGain;
        v.lastSendR = outR * v.sendGain;

        ++v.elapsed;

        if (v.stage == Voice::Stage::done)
            finished = true;

        // Past the end of the sample: let effect/filter tails ring out, then stop.
        if (exhausted && c.fxType != fxLoop)
        {
            const bool quiet = std::abs (outL) + std::abs (outR) < 1.0e-4f;
            const bool tailHasSound = c.fxType == fxSubOsc ? v.fxEnv * c.amount > 1.0e-3f : false;
            if ((quiet && ! tailHasSound) || v.position > endFrame + 10.0 * sampleRateF * v.rateRatio)
                finished = true;
        }

        if (finished)
            break;
    }

    if (finished)
        killVoice (v); // ends at (near) zero: envelope, fade or the sample itself ran out
}

} // namespace rb
