namespace rb
{

DeviceSpec Dynamics::createSpec()
{
    DeviceSpec spec;
    spec.typeId = "dynamics";
    spec.displayName = "Dynamics";

    auto& p = spec.params;
    p.push_back (ParamSpec::continuous ("threshold", "Threshold", -60.0f, 0.0f, -18.0f, ParamUnit::decibels));
    p.push_back (ParamSpec::continuous ("ratio", "Ratio", 1.0f, 20.0f, 4.0f, ParamUnit::ratio, 4.0f));
    p.push_back (ParamSpec::continuous ("attack", "Attack", 0.0005f, 0.1f, 0.01f, ParamUnit::seconds, 0.01f));
    p.push_back (ParamSpec::continuous ("release", "Release", 0.01f, 1.5f, 0.15f, ParamUnit::seconds, 0.15f));
    p.push_back (ParamSpec::toggle ("makeup", "Auto Gain", true));
    p.push_back (ParamSpec::continuous ("low", "Low", -12.0f, 12.0f, 0.0f, ParamUnit::decibels));
    p.push_back (ParamSpec::continuous ("high", "High", -12.0f, 12.0f, 0.0f, ParamUnit::decibels));
    p.push_back (ParamSpec::continuous ("output", "Output", -24.0f, 12.0f, 0.0f, ParamUnit::decibels));
    return spec;
}

void Dynamics::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    for (auto* s : { &threshold, &ratio, &low, &high, &output })
        s->prepare (sampleRate, 0.02);
    makeup.prepare (sampleRate, 0.08);
    snapNext = true;
    reset();
}

void Dynamics::reset()
{
    envelopeDb = 0.0f;
    for (auto& channel : eq)
        for (auto& f : channel)
            f.reset();
}

void Dynamics::setParameters (const float* values, int numValues)
{
    const auto get = [&] (int index, float fallback) { return fx::param (values, numValues, index, fallback); };

    const float thr = juce::jlimit (-60.0f, 0.0f, get (pThreshold, -18.0f));
    const float r = juce::jlimit (1.0f, 20.0f, get (pRatio, 4.0f));
    threshold.setTarget (thr);
    ratio.setTarget (r);
    attackCoeff = fx::timeCoeff (sampleRate, juce::jlimit (0.0005f, 0.1f, get (pAttack, 0.01f)));
    releaseCoeff = fx::timeCoeff (sampleRate, juce::jlimit (0.01f, 1.5f, get (pRelease, 0.15f)));

    // Auto gain restores half of the reduction a full-scale signal would get.
    const bool autoGain = get (pMakeup, 1.0f) >= 0.5f;
    makeup.setTarget (autoGain ? juce::jmin (18.0f, 0.5f * (1.0f - 1.0f / r) * -thr) : 0.0f);

    low.setTarget (juce::jlimit (-12.0f, 12.0f, get (pLow, 0.0f)));
    high.setTarget (juce::jlimit (-12.0f, 12.0f, get (pHigh, 0.0f)));
    output.setTarget (juce::jlimit (-24.0f, 12.0f, get (pOutput, 0.0f)));

    if (snapNext)
    {
        for (auto* s : { &threshold, &ratio, &makeup, &low, &high, &output })
            s->snapToTarget();
        updateEq();
        eqCoeffs = eqTargets;
        snapNext = false;
    }
}

void Dynamics::updateEq()
{
    eqTargets[0] = fx::EqCoeffs::lowShelf (sampleRate, 200.0, 0.7071, low.get());
    eqTargets[1] = fx::EqCoeffs::highShelf (sampleRate, 4000.0, 0.7071, high.get());
}

void Dynamics::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2 || numSamples <= 0)
        return;

    float* channels[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };

    for (int start = 0; start < numSamples; start += fx::chunkSize)
    {
        const int len = juce::jmin (fx::chunkSize, numSamples - start);

        const bool eqMoving = low.isSmoothing() || high.isSmoothing();
        if (eqMoving)
        {
            low.skip (len);
            high.skip (len);
            updateEq();
            for (size_t b = 0; b < 2; ++b)
                eqSteps[b] = fx::EqCoeffs::delta (eqCoeffs[b], eqTargets[b], len);
        }

        const float thr = threshold.skip (len);
        const float slope = 1.0f / ratio.skip (len) - 1.0f; // <= 0
        const float kneeStart = fx::dbToGain (thr - 0.5f * kneeDb);

        for (int i = start; i < start + len; ++i)
        {
            const float inL = channels[0][i], inR = channels[1][i];
            const float level = juce::jmax (std::abs (inL), std::abs (inR));

            // Static curve with a quadratic soft knee.
            float reduction = 0.0f;
            if (level > kneeStart)
            {
                const float over = 20.0f * std::log10 (level) - thr;
                if (over >= 0.5f * kneeDb)
                    reduction = slope * over;
                else
                {
                    const float x = over + 0.5f * kneeDb;
                    reduction = slope * x * x / (2.0f * kneeDb);
                }
            }

            envelopeDb += (reduction < envelopeDb ? attackCoeff : releaseCoeff) * (reduction - envelopeDb);

            const float gain = fx::dbToGain (envelopeDb + makeup.next() + output.next());
            if (eqMoving)
                for (size_t b = 0; b < 2; ++b)
                    eqCoeffs[b].advance (eqSteps[b]);

            for (int ch = 0; ch < 2; ++ch)
            {
                auto& f = eq[(size_t) ch];
                const float x = (ch == 0 ? inL : inR) * gain;
                channels[ch][i] = f[1].process (eqCoeffs[1], f[0].process (eqCoeffs[0], x));
            }
        }

        if (eqMoving)
            eqCoeffs = eqTargets;
    }
}

} // namespace rb
