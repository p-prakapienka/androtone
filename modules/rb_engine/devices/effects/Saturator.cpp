namespace rb
{

DeviceSpec Saturator::createSpec()
{
    DeviceSpec spec;
    spec.typeId = "saturator";
    spec.displayName = "Saturator";

    auto& p = spec.params;
    p.push_back (ParamSpec::choice ("curve", "Curve", { "Soft", "Analog", "Tape", "Hard", "Fold", "Bit" }, 1));
    p.push_back (ParamSpec::continuous ("drive", "Drive", 0.0f, 36.0f, 6.0f, ParamUnit::decibels, 12.0f));
    p.push_back (ParamSpec::continuous ("colour", "Colour", -1.0f, 1.0f, 0.0f, ParamUnit::percent));
    p.push_back (ParamSpec::continuous ("output", "Output", -24.0f, 12.0f, 0.0f, ParamUnit::decibels));
    p.push_back (ParamSpec::continuous ("mix", "Dry/Wet", 0.0f, 1.0f, 1.0f, ParamUnit::percent));
    return spec;
}

void Saturator::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    maxBlock = juce::jmax (16, maxBlockSize);

    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) 2, (size_t) 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    oversampling->initProcessing ((size_t) maxBlock);
    dryBuffer.setSize (2, maxBlock, false, true, false);

    driveDb.prepare (sampleRate * 2.0, 0.01);
    for (auto* s : { &colour, &outputDb, &mix })
        s->prepare (sampleRate, 0.01);

    curveFadeStep = (float) (1.0 / (0.01 * sampleRate * 2.0));
    tiltCoeff = fx::onePoleCoeff (sampleRate, 700.0);
    dcCoeff = 1.0f - fx::twoPi * 8.0f / (float) sampleRate;
    snapNext = true;
    reset();
}

void Saturator::reset()
{
    if (oversampling != nullptr)
        oversampling->reset();
    tiltState = {};
    dcInput = {};
    dcOutput = {};
}

void Saturator::setParameters (const float* values, int numValues)
{
    const auto get = [&] (int index, float fallback) { return fx::param (values, numValues, index, fallback); };

    const int newCurve = fx::choice (get (pCurve, 1.0f), numCurves);
    driveDb.setTarget (juce::jlimit (0.0f, 36.0f, get (pDrive, 6.0f)));
    colour.setTarget (juce::jlimit (-1.0f, 1.0f, get (pColour, 0.0f)));
    outputDb.setTarget (juce::jlimit (-24.0f, 12.0f, get (pOutput, 0.0f)));
    mix.setTarget (juce::jlimit (0.0f, 1.0f, get (pMix, 1.0f)));

    if (snapNext)
    {
        for (auto* s : { &driveDb, &colour, &outputDb, &mix })
            s->snapToTarget();
        curve = previousCurve = newCurve;
        curveFade = 1.0f;
        snapNext = false;
    }
    else if (newCurve != curve && curveFade >= 1.0f)
    {
        previousCurve = curve;
        curve = newCurve;
        curveFade = 0.0f;
    }
}

float Saturator::shape (int c, float x)
{
    switch (c)
    {
        case soft:   return fx::fastTanh (x);
        case analog:
        {
            // Biased tanh: asymmetric, adds even harmonics (DC is removed afterwards).
            constexpr float bias = 0.25f;
            const float offset = fx::fastTanh (bias);
            return (fx::fastTanh (x + bias) - offset) / (1.0f - offset * offset);
        }
        case tape:   return x / std::sqrt (1.0f + x * x);
        case hard:   return juce::jlimit (-1.0f, 1.0f, x);
        case fold:   return std::sin (x);
        case bit:
        default:     return std::round (fx::fastTanh (x) * 12.0f) * (1.0f / 12.0f);
    }
}

void Saturator::processChunk (float* const* channels, int numSamples)
{
    // Colour: tilt around 700 Hz before the shaper, +-6 dB at each end.
    const bool tiltActive = colour.isSmoothing() || std::abs (colour.getTarget()) > 1.0e-4f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float c = colour.next();
        if (! tiltActive)
            continue;
        const float high = fx::dbToGain (6.0f * c);
        const float low = 1.0f / high;
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const float x = channels[ch][i];
            tiltState[ch] += tiltCoeff * (x - tiltState[ch]);
            channels[ch][i] = tiltState[ch] * low + (x - tiltState[ch]) * high;
        }
    }

    juce::dsp::AudioBlock<float> block (channels, 2, (size_t) numSamples);
    auto up = oversampling->processSamplesUp (block);
    float* upL = up.getChannelPointer (0);
    float* upR = up.getChannelPointer (1);
    const int upSamples = (int) up.getNumSamples();

    const bool driveMoving = driveDb.isSmoothing();
    float gain = fx::dbToGain (driveDb.get());

    for (int i = 0; i < upSamples; ++i)
    {
        if (driveMoving)
            gain = fx::dbToGain (driveDb.next());
        const float l = upL[i] * gain, r = upR[i] * gain;
        float yl = shape (curve, l), yr = shape (curve, r);
        if (curveFade < 1.0f)
        {
            const float pl = shape (previousCurve, l), pr = shape (previousCurve, r);
            yl = pl + curveFade * (yl - pl);
            yr = pr + curveFade * (yr - pr);
            curveFade = juce::jmin (1.0f, curveFade + curveFadeStep);
        }
        upL[i] = yl;
        upR[i] = yr;
    }

    oversampling->processSamplesDown (block);
}

void Saturator::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2 || numSamples <= 0 || oversampling == nullptr)
        return;

    for (int offset = 0; offset < numSamples; offset += maxBlock)
    {
        const int n = juce::jmin (maxBlock, numSamples - offset);
        float* channels[2] = { buffer.getWritePointer (0, offset), buffer.getWritePointer (1, offset) };

        for (int ch = 0; ch < 2; ++ch)
            dryBuffer.copyFrom (ch, 0, channels[ch], n);

        processChunk (channels, n);

        const float* dry[2] = { dryBuffer.getReadPointer (0), dryBuffer.getReadPointer (1) };
        const bool outputMoving = outputDb.isSmoothing();
        float out = fx::dbToGain (outputDb.get());

        for (int i = 0; i < n; ++i)
        {
            if (outputMoving)
                out = fx::dbToGain (outputDb.next());
            const float m = mix.next();
            for (size_t ch = 0; ch < 2; ++ch)
            {
                // DC blocker after the shaper (the Analog curve is asymmetric).
                const float x = channels[ch][i];
                const float y = x - dcInput[ch] + dcCoeff * dcOutput[ch];
                dcInput[ch] = x;
                dcOutput[ch] = y;
                channels[ch][i] = fx::mixDryWet (dry[ch][i], y * out, m);
            }
        }
    }
}

} // namespace rb
