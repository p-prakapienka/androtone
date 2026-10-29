namespace rb
{

DeviceSpec Crusher::createSpec()
{
    DeviceSpec spec;
    spec.typeId = "crusher";
    spec.displayName = "Crusher";

    auto& p = spec.params;
    p.push_back (ParamSpec::continuous ("bits", "Bits", 1.0f, 16.0f, 8.0f, ParamUnit::none, 6.0f));
    p.push_back (ParamSpec::continuous ("rate", "Sample Rate", 200.0f, 48000.0f, 12000.0f, ParamUnit::hertz, 6000.0f));
    p.push_back (ParamSpec::continuous ("jitter", "Jitter", 0.0f, 1.0f, 0.0f, ParamUnit::percent));
    p.push_back (ParamSpec::choice ("filter", "Filter", { "Off", "Pre", "Post" }, 2));
    p.push_back (ParamSpec::continuous ("cutoff", "Cutoff", 200.0f, 20000.0f, 8000.0f, ParamUnit::hertz, 3000.0f));
    p.push_back (ParamSpec::continuous ("mix", "Dry/Wet", 0.0f, 1.0f, 1.0f, ParamUnit::percent));
    return spec;
}

void Crusher::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    for (auto* s : { &bits, &rate, &jitter, &mix })
        s->prepare (sampleRate, 0.01);
    cutoffOct.prepare (sampleRate, 0.02);
    filterFade.prepare (sampleRate);
    for (auto& f : filters)
        f.prepare (sampleRate);
    snapNext = true;
    reset();
}

void Crusher::reset()
{
    for (auto& f : filters)
        f.reset();
    holdPhase = 0.0;
    holdLength = 1.0;
    held = {};
}

void Crusher::setParameters (const float* values, int numValues)
{
    const auto get = [&] (int index, float fallback) { return fx::param (values, numValues, index, fallback); };

    bits.setTarget (juce::jlimit (1.0f, 16.0f, get (pBits, 8.0f)));
    rate.setTarget (juce::jlimit (200.0f, 48000.0f, get (pRate, 12000.0f)));
    jitter.setTarget (juce::jlimit (0.0f, 1.0f, get (pJitter, 0.0f)));
    cutoffOct.setTarget (std::log2 (juce::jlimit (200.0f, 20000.0f, get (pCutoff, 8000.0f))));
    mix.setTarget (juce::jlimit (0.0f, 1.0f, get (pMix, 1.0f)));
    const int filterMode = fx::choice (get (pFilter, 2.0f), 3);

    if (snapNext)
    {
        for (auto* s : { &bits, &rate, &jitter, &cutoffOct, &mix })
            s->snapToTarget();
        filterFade.snap (filterMode);
        snapNext = false;
    }
    else
    {
        filterFade.request (filterMode);
    }
}

void Crusher::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2 || numSamples <= 0)
        return;

    float* channels[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };

    for (int start = 0; start < numSamples; start += fx::chunkSize)
    {
        const int len = juce::jmin (fx::chunkSize, numSamples - start);

        const float cutoffHz = std::exp2 (cutoffOct.skip (len));
        for (auto& f : filters)
            f.set (cutoffHz, 0.15f);

        // Quantiser: 2^(bits-1) steps per polarity, continuous in bits.
        const float levels = std::exp2 (bits.skip (len) - 1.0f);
        const float invLevels = 1.0f / levels;
        const double increment = juce::jmin (1.0, (double) rate.skip (len) / sampleRate);
        const float jitterAmount = jitter.skip (len);

        for (int i = start; i < start + len; ++i)
        {
            bool switched = false;
            const float filterGain = filterFade.next (switched);
            if (switched)
                for (auto& f : filters)
                    f.reset();
            const int filterMode = filterFade.mode();

            // Sample and hold, shared by both channels; jitter randomises the hold length.
            holdPhase += increment;
            const bool sampleNow = holdPhase >= holdLength;
            if (sampleNow)
            {
                holdPhase -= holdLength;
                holdLength = 1.0 + (double) (jitterAmount * 0.9f * noise.next());
                holdPhase = juce::jmin (holdPhase, holdLength);
            }

            const float m = mix.next();

            for (size_t ch = 0; ch < 2; ++ch)
            {
                const float dry = channels[ch][i];
                float x = dry;
                if (filterMode == filterPre)
                    x += filterGain * (filters[ch].process (x, dsp::Svf::Mode::lowpass) - x);

                if (sampleNow)
                    held[ch] = std::round (x * levels) * invLevels;
                float wet = held[ch];

                if (filterMode == filterPost)
                    wet += filterGain * (filters[ch].process (wet, dsp::Svf::Mode::lowpass) - wet);

                channels[ch][i] = fx::mixDryWet (dry, wet, m);
            }
        }
    }
}

} // namespace rb
