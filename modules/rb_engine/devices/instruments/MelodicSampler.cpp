namespace rb
{

DeviceSpec MelodicSampler::createSpec()
{
    const juce::String main = "Main", global = "Global", sampleGroup = "Sample";

    DeviceSpec spec;
    spec.typeId = "sampler";
    spec.displayName = "Sampler";
    auto& p = spec.params;
    p.push_back (ParamSpec::choice ("filterType", "Filter", { "LP", "BP", "HP" }, 0, main));
    p.push_back (ParamSpec::continuous ("cutoff", "Cutoff", 20.0f, 20000.0f, 20000.0f, ParamUnit::hertz, 1000.0f, main));
    p.push_back (ParamSpec::continuous ("resonance", "Resonance", 0.0f, 1.0f, 0.1f, ParamUnit::percent, 0.0f, main));
    p.push_back (ParamSpec::continuous ("transpose", "Transpose", -48.0f, 48.0f, 0.0f, ParamUnit::semitones, 0.0f, main));
    p.push_back (ParamSpec::continuous ("detune", "Detune", -50.0f, 50.0f, 0.0f, ParamUnit::cents, 0.0f, main));
    p.push_back (ParamSpec::continuous ("attack", "Attack", 0.0f, 5.0f, 0.0f, ParamUnit::seconds, 0.2f, main));
    p.push_back (ParamSpec::continuous ("decay", "Decay", 0.01f, 20.0f, 20.0f, ParamUnit::seconds, 1.0f, main));
    p.push_back (ParamSpec::choice ("envMode", "Mode", { "Trig", "Gate" }, 1, main));
    p.push_back (ParamSpec::continuous ("release", "Release", 0.001f, 10.0f, 0.3f, ParamUnit::seconds, 0.5f, main));
    p.push_back (ParamSpec::continuous ("filterLfo", "Filter LFO", 0.0f, 1.0f, 0.0f, ParamUnit::percent, 0.0f, main));
    p.push_back (ParamSpec::continuous ("lfoRate", "LFO Rate", 0.05f, 20.0f, 4.0f, ParamUnit::hertz, 2.0f, global));
    p.push_back (ParamSpec::continuous ("volume", "Volume", -70.0f, 6.0f, 0.0f, ParamUnit::decibels, 0.0f, global));
    p.push_back (ParamSpec::continuous ("gain", "Gain", -70.0f, 24.0f, 0.0f, ParamUnit::decibels, 0.0f, sampleGroup));
    p.push_back (ParamSpec::continuous ("sampleStart", "Start", 0.0f, 1.0f, 0.0f, ParamUnit::percent, 0.0f, sampleGroup));
    spec.groups = { main, global, sampleGroup };
    return spec;
}

namespace melodicsampler
{
    constexpr int samplerChunkSize = 16;
    constexpr float samplerInfiniteDecay = 19.99f;
}

MelodicSampler::MelodicSampler()
{
    const auto defaults = createSpec().defaults();
    jassert ((int) defaults.size() == numParams);
    for (size_t i = 0; i < values.size() && i < defaults.size(); ++i)
        values[i] = defaults[i];
}

void MelodicSampler::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    volume.prepare (sampleRate, 0.02f);
    volume.snap (dsp::dbToGain (values[pVolume]));
    declicker.prepare (sampleRate);
    logCutoff = std::log2 (juce::jlimit (20.0f, 20000.0f, values[pCutoff]));
    for (auto& v : voices)
        for (auto& f : v.filters)
            f.prepare (sampleRate);
    reset();
}

void MelodicSampler::reset()
{
    for (auto& v : voices)
        v.active = false;
    declicker.reset();
    lfoPhase = 0.0;
}

void MelodicSampler::setParameters (const float* newValues, int numValues)
{
    if (newValues == nullptr)
        return;
    const int n = juce::jmin (numValues, (int) numParams);
    for (int i = 0; i < n; ++i)
        values[(size_t) i] = newValues[i];
    volume.setTarget (dsp::dbToGain (values[pVolume]));
}

void MelodicSampler::setSample (int slot, const SampleData* newSample)
{
    if (slot != 0 || newSample == sample)
        return;

    // The old pointer may be freed after this call: stop every voice still reading it.
    for (auto& v : voices)
        if (v.active)
            killVoice (v);
    sample = newSample;
}

void MelodicSampler::allNotesOff()
{
    for (auto& v : voices)
        if (v.active)
            killVoice (v);
}

int MelodicSampler::getNumActiveVoices() const
{
    int n = 0;
    for (auto& v : voices)
        n += v.active ? 1 : 0;
    return n;
}

void MelodicSampler::killVoice (Voice& v)
{
    declicker.add (v.lastL, v.lastR);
    v.active = false;
}

void MelodicSampler::beginFade (Voice& v, float seconds)
{
    const float step = 1.0f / juce::jmax (1.0f, seconds * (float) sampleRate);
    v.fadeStep = v.fading ? juce::jmax (v.fadeStep, step) : step;
    v.fading = true;
}

MelodicSampler::Voice* MelodicSampler::allocateVoice()
{
    int sounding = 0;
    Voice* oldest = nullptr;
    for (auto& v : voices)
    {
        if (! v.active || v.fading)
            continue;
        ++sounding;
        // Prefer stealing released voices, then the oldest.
        const bool released = v.stage == Voice::Stage::release;
        const bool oldestReleased = oldest != nullptr && oldest->stage == Voice::Stage::release;
        if (oldest == nullptr || (released && ! oldestReleased) || (released == oldestReleased && v.age < oldest->age))
            oldest = &v;
    }

    if (sounding >= maxVoices && oldest != nullptr)
        beginFade (*oldest, 0.005f);

    for (auto& v : voices)
        if (! v.active)
            return &v;

    Voice* victim = nullptr;
    for (auto& v : voices)
        if (v.fading && (victim == nullptr || v.fadeGain < victim->fadeGain))
            victim = &v;
    if (victim == nullptr)
        victim = oldest != nullptr ? oldest : &voices[0];
    killVoice (*victim);
    return victim;
}

void MelodicSampler::handleEvent (const NoteEvent& e)
{
    if (e.isNoteOn())
    {
        startVoice (e);
        return;
    }

    for (auto& v : voices)
    {
        if (! v.active || v.note != e.pitch || ! v.held)
            continue;
        v.held = false;
        if (v.gateMode && v.stage != Voice::Stage::done)
            v.stage = Voice::Stage::release;
    }
}

void MelodicSampler::startVoice (const NoteEvent& e)
{
    notePressure[(size_t) juce::jlimit (0, 127, e.pitch)] = 0.0f; // a new note starts without poly pressure

    if (sample == nullptr || sample->getNumFrames() <= 0 || sample->buffer.getNumChannels() <= 0)
        return;

    // Retriggering a note fades out its previous voice.
    for (auto& v : voices)
        if (v.active && v.note == e.pitch && ! v.fading)
            beginFade (v, 0.01f);

    Voice* v = allocateVoice();
    *v = Voice();
    for (auto& f : v->filters)
        f.prepare (sampleRate);

    const float velocity = juce::jlimit (0.0f, 1.0f, e.velocity);
    v->active = true;
    v->note = e.pitch;
    v->age = nextAge++;
    v->velocityGain = std::pow (velocity, 1.3f);
    v->position = (double) juce::jlimit (0.0f, 1.0f, values[pSampleStart]) * (double) (sample->getNumFrames() - 1);
    v->gateMode = juce::roundToInt (values[pEnvMode]) == 1;
    v->held = true;
    v->gain = dsp::dbToGain (values[pGain]) * v->velocityGain;
}

void MelodicSampler::process (juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>&,
                              const NoteEvent* events, int numEvents, int numSamples)
{
    float* dl = dry.getWritePointer (0);
    float* dr = dry.getWritePointer (dry.getNumChannels() > 1 ? 1 : 0);

    dsp::renderWithEvents (events, numEvents, numSamples,
                           [this] (const NoteEvent& e) { handleEvent (e); },
                           [&] (int start, int count) { renderRange (dl, dr, start, count); });

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = volume.next();
        dl[i] *= g;
        dr[i] *= g;
    }
}

void MelodicSampler::setPressure (int note, float pressure)
{
    pressure = juce::jlimit (0.0f, 1.0f, pressure);
    if (note < 0)
        channelPressure = pressure;
    else if (note < 128)
        notePressure[(size_t) note] = pressure;
}

void MelodicSampler::renderRange (float* dl, float* dr, int start, int count)
{
    const float lfoAmount = juce::jlimit (0.0f, 1.0f, values[pFilterLfo]);
    const double lfoIncrement = (double) juce::jmax (0.0f, values[pLfoRate]) / sampleRate;
    const float sampleGain = dsp::dbToGain (values[pGain]);

    for (int done = 0; done < count;)
    {
        const int n = juce::jmin (melodicsampler::samplerChunkSize, count - done);
        const int offset = start + done;

        // One LFO for the instrument sweeps the cutoff by up to +-4 octaves.
        const float lfo = std::sin ((float) (lfoPhase * juce::MathConstants<double>::twoPi));
        lfoPhase += lfoIncrement * n;
        lfoPhase -= std::floor (lfoPhase);

        const float target = std::log2 (juce::jlimit (20.0f, 20000.0f, values[pCutoff])) + lfo * lfoAmount * 4.0f;
        logCutoff += (target - logCutoff) * 0.25f;
        bend += (bendTarget - bend) * 0.25f;
        const float cutoff = juce::jlimit (20.0f, 20000.0f, std::exp2 (logCutoff));

        for (auto& v : voices)
            if (v.active)
                renderVoice (v, dl + offset, dr + offset, n, cutoff, sampleGain * v.velocityGain);

        declicker.render (dl + offset, dr + offset, nullptr, nullptr, n);
        done += n;
    }
}

void MelodicSampler::renderVoice (Voice& v, float* dl, float* dr, int count, float cutoff, float gainTarget)
{
    const auto& s = *sample;
    const double frames = (double) s.getNumFrames();
    const bool stereo = s.buffer.getNumChannels() > 1;
    const float sr = (float) sampleRate;

    const float semitones = (float) (v.note - samplerRootNote) + values[pTranspose] + values[pDetune] * 0.01f + bend;

    // Pressure opens the filter by up to two octaves.
    const float pressure = juce::jmax (channelPressure, notePressure[(size_t) juce::jlimit (0, 127, v.note)]);
    if (pressure > 0.0f)
        cutoff = juce::jlimit (20.0f, 20000.0f, cutoff * std::exp2 (pressure * 2.0f));
    const double increment = s.sampleRate / sampleRate * std::exp2 ((double) semitones / 12.0);

    const int filterType = juce::roundToInt (values[pFilterType]);
    const float resonance = juce::jlimit (0.0f, 1.0f, values[pResonance]);
    v.filters[0].set (cutoff, resonance);
    if (stereo)
        v.filters[1].set (cutoff, resonance);
    const auto mode = filterType == 1 ? dsp::Svf::Mode::bandpass
                    : filterType == 2 ? dsp::Svf::Mode::highpass
                                      : dsp::Svf::Mode::lowpass;

    const float attackSamples = juce::jmax (0.001f, values[pAttack]) * sr;
    const float decay = values[pDecay];
    const float decaySamples = decay >= melodicsampler::samplerInfiniteDecay ? -1.0f : juce::jmax (0.01f, decay) * sr;
    const float releaseCoeff = std::exp (-6.9f / (juce::jmax (0.001f, values[pRelease]) * sr));

    const float gainStep = (gainTarget - v.gain) / (float) count;
    bool finished = false;

    for (int i = 0; i < count; ++i)
    {
        const float fade = sampler::endFade (s, v.position);
        auto x = sampler::readFrame (s, v.position);
        v.position += increment;

        x.l = v.filters[0].process (x.l * fade, mode);
        x.r = stereo ? v.filters[1].process (x.r * fade, mode) : x.l;

        switch (v.stage)
        {
            case Voice::Stage::attack:
                v.envPhase += 1.0f / attackSamples;
                if (v.envPhase >= 1.0f)
                {
                    v.envLevel = 1.0f;
                    v.envPhase = 0.0f;
                    v.stage = Voice::Stage::decay;
                }
                else
                {
                    v.envLevel = v.envPhase;
                }
                break;

            case Voice::Stage::decay:
                if (decaySamples > 0.0f)
                    v.envPhase += 1.0f / decaySamples;
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
                v.envLevel = v.envLevel * releaseCoeff - 1.0e-7f;
                if (v.envLevel <= 1.0e-4f)
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

        v.gain += gainStep;
        const float outL = x.l * gain * v.gain;
        const float outR = x.r * gain * v.gain;
        dl[i] += outL;
        dr[i] += outR;
        v.lastL = outL;
        v.lastR = outR;

        if (v.stage == Voice::Stage::done)
            finished = true;
        if (v.position >= frames + 4.0 && std::abs (outL) + std::abs (outR) < 1.0e-4f)
            finished = true;
        if (v.position >= frames + 0.05 * s.sampleRate)
            finished = true; // filter tail is over by now

        if (finished)
            break;
    }

    if (finished)
        killVoice (v); // last output is (near) zero; the declicker absorbs any filter tail
}

} // namespace rb
