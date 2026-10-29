namespace rb
{

DeviceSpec ChannelEq::createSpec()
{
    DeviceSpec spec;
    spec.typeId = "eq";
    spec.displayName = "EQ";

    auto& p = spec.params;
    p.push_back (ParamSpec::continuous ("lowCut", "Low Cut", 10.0f, 1000.0f, 10.0f, ParamUnit::hertz, 100.0f));
    p.push_back (ParamSpec::continuous ("low", "Low", -15.0f, 15.0f, 0.0f, ParamUnit::decibels));
    p.push_back (ParamSpec::continuous ("mid", "Mid", -15.0f, 15.0f, 0.0f, ParamUnit::decibels));
    p.push_back (ParamSpec::continuous ("midFreq", "Mid Freq", 100.0f, 10000.0f, 1000.0f, ParamUnit::hertz, 1000.0f));
    p.push_back (ParamSpec::continuous ("high", "High", -15.0f, 15.0f, 0.0f, ParamUnit::decibels));
    p.push_back (ParamSpec::continuous ("output", "Output", -24.0f, 12.0f, 0.0f, ParamUnit::decibels));
    return spec;
}

void ChannelEq::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    for (auto* s : { &lowCut, &low, &mid, &midFreq, &high, &output })
        s->prepare (sampleRate, 0.02);
    snapNext = true;
    reset();
}

void ChannelEq::reset()
{
    for (auto& channel : filters)
        for (auto& f : channel)
            f.reset();
}

void ChannelEq::setParameters (const float* values, int numValues)
{
    const auto get = [&] (int index, float fallback) { return fx::param (values, numValues, index, fallback); };

    // Frequencies are smoothed in octaves so sweeps sound even.
    lowCut.setTarget (std::log2 (juce::jlimit (10.0f, 1000.0f, get (pLowCut, 10.0f))));
    low.setTarget (juce::jlimit (-15.0f, 15.0f, get (pLow, 0.0f)));
    mid.setTarget (juce::jlimit (-15.0f, 15.0f, get (pMid, 0.0f)));
    midFreq.setTarget (std::log2 (juce::jlimit (100.0f, 10000.0f, get (pMidFreq, 1000.0f))));
    high.setTarget (juce::jlimit (-15.0f, 15.0f, get (pHigh, 0.0f)));
    output.setTarget (juce::jlimit (-24.0f, 12.0f, get (pOutput, 0.0f)));

    if (snapNext)
    {
        for (auto* s : { &lowCut, &low, &mid, &midFreq, &high, &output })
            s->snapToTarget();
        updateCoefficients();
        coeffs = targets;
        snapNext = false;
    }
}

void ChannelEq::updateCoefficients()
{
    targets[0] = fx::EqCoeffs::highPass (sampleRate, std::exp2 (lowCut.get()), 0.7071);
    targets[1] = fx::EqCoeffs::lowShelf (sampleRate, 120.0, 0.7071, low.get());
    targets[2] = fx::EqCoeffs::peak (sampleRate, std::exp2 (midFreq.get()), 0.8, mid.get());
    targets[3] = fx::EqCoeffs::highShelf (sampleRate, 6000.0, 0.7071, high.get());
}

void ChannelEq::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2 || numSamples <= 0)
        return;

    float* channels[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };

    for (int start = 0; start < numSamples; start += fx::chunkSize)
    {
        const int len = juce::jmin (fx::chunkSize, numSamples - start);

        bool changed = false;
        for (auto* s : { &lowCut, &low, &mid, &midFreq, &high })
            if (s->isSmoothing())
            {
                s->skip (len);
                changed = true;
            }
        if (changed)
        {
            updateCoefficients();
            for (size_t b = 0; b < (size_t) numBands; ++b)
                steps[b] = fx::EqCoeffs::delta (coeffs[b], targets[b], len);
        }

        const bool gainMoving = output.isSmoothing();
        float gain = fx::dbToGain (output.get());

        for (int i = start; i < start + len; ++i)
        {
            if (gainMoving)
                gain = fx::dbToGain (output.next());
            if (changed)
                for (size_t b = 0; b < (size_t) numBands; ++b)
                    coeffs[b].advance (steps[b]);

            for (int ch = 0; ch < 2; ++ch)
            {
                auto& f = filters[(size_t) ch];
                float x = channels[ch][i];
                for (size_t b = 0; b < (size_t) numBands; ++b)
                    x = f[b].process (coeffs[b], x);
                channels[ch][i] = x * gain;
            }
        }

        if (changed)
            coeffs = targets; // lands exactly, without accumulated rounding
    }
}

} // namespace rb
