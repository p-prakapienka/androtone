namespace rb
{

DeviceSpec AutoFilter::createSpec()
{
    DeviceSpec spec;
    spec.typeId = "autoFilter";
    spec.displayName = "Auto Filter";
    spec.groups = { "Filter", "LFO" };

    auto& p = spec.params;
    p.push_back (ParamSpec::choice ("type", "Type", { "Lowpass", "Lowpass 24", "Bandpass", "Highpass", "Highpass 24", "Notch" }, 0, "Filter"));
    p.push_back (ParamSpec::continuous ("cutoff", "Frequency", 20.0f, 20000.0f, 1200.0f, ParamUnit::hertz, 1000.0f, "Filter"));
    p.push_back (ParamSpec::continuous ("resonance", "Resonance", 0.0f, 1.0f, 0.3f, ParamUnit::percent, 0.0f, "Filter"));
    p.push_back (ParamSpec::continuous ("drive", "Drive", 0.0f, 24.0f, 0.0f, ParamUnit::decibels, 0.0f, "Filter"));
    p.push_back (ParamSpec::continuous ("envelope", "Envelope", -1.0f, 1.0f, 0.0f, ParamUnit::percent, 0.0f, "Filter"));
    p.push_back (ParamSpec::continuous ("lfoAmount", "LFO Amount", 0.0f, 1.0f, 0.25f, ParamUnit::percent, 0.0f, "LFO"));
    p.push_back (ParamSpec::toggle ("lfoSync", "Sync", false, "LFO"));
    p.push_back (ParamSpec::continuous ("lfoRate", "Rate", 0.02f, 20.0f, 1.0f, ParamUnit::hertz, 1.0f, "LFO"));
    p.push_back (ParamSpec::choice ("lfoDivision", "Sync Rate", fx::lfoDivisionNames(), 5, "LFO"));
    p.push_back (ParamSpec::choice ("lfoShape", "Shape", { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random" }, 0, "LFO"));
    p.push_back (ParamSpec::continuous ("lfoSpread", "Spread", 0.0f, 1.0f, 0.0f, ParamUnit::percent, 0.0f, "LFO"));
    p.push_back (ParamSpec::continuous ("mix", "Dry/Wet", 0.0f, 1.0f, 1.0f, ParamUnit::percent, 0.0f, "Filter"));
    return spec;
}

void AutoFilter::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    for (auto* s : { &cutoffOct, &resonance, &driveDb, &envAmount, &lfoAmount, &lfoSpread, &mix })
        s->prepare (sampleRate, 0.01);
    cutoffOct.prepare (sampleRate, 0.02);
    typeFade.prepare (sampleRate);

    for (auto& f : stageA) f.prepare (sampleRate);
    for (auto& f : stageB) f.prepare (sampleRate);

    lfoSmoothCoeff = fx::timeCoeff (sampleRate / fx::chunkSize, 0.003);
    envAttack = fx::timeCoeff (sampleRate, 0.003);
    envRelease = fx::timeCoeff (sampleRate, 0.12);
    snapNext = true;
    reset();
}

void AutoFilter::reset()
{
    for (auto& f : stageA) f.reset();
    for (auto& f : stageB) f.reset();
    lfoPhase = 0.0;
    lastChannelPhase = {};
    randomHeld = {};
    lfoSmooth = {};
    envLevel = 0.0f;
}

void AutoFilter::setParameters (const float* values, int numValues)
{
    const auto get = [&] (int index, float fallback) { return fx::param (values, numValues, index, fallback); };

    const int type = fx::choice (get (pType, 0.0f), numTypes);
    cutoffOct.setTarget (std::log2 (juce::jlimit (20.0f, 20000.0f, get (pCutoff, 1200.0f))));
    resonance.setTarget (juce::jlimit (0.0f, 1.0f, get (pResonance, 0.3f)));
    driveDb.setTarget (juce::jlimit (0.0f, 24.0f, get (pDrive, 0.0f)));
    envAmount.setTarget (juce::jlimit (-1.0f, 1.0f, get (pEnvelope, 0.0f)));
    lfoAmount.setTarget (juce::jlimit (0.0f, 1.0f, get (pLfoAmount, 0.25f)));
    lfoSync = get (pLfoSync, 0.0f) >= 0.5f;
    lfoRateHz = juce::jlimit (0.02f, 20.0f, get (pLfoRate, 1.0f));
    lfoDivision = fx::choice (get (pLfoDivision, 5.0f), fx::numLfoDivisions);
    lfoShape = fx::choice (get (pLfoShape, 0.0f), 6);
    lfoSpread.setTarget (juce::jlimit (0.0f, 1.0f, get (pLfoSpread, 0.0f)));
    mix.setTarget (juce::jlimit (0.0f, 1.0f, get (pMix, 1.0f)));

    if (snapNext)
    {
        for (auto* s : { &cutoffOct, &resonance, &driveDb, &envAmount, &lfoAmount, &lfoSpread, &mix })
            s->snapToTarget();
        typeFade.snap (type);
        snapNext = false;
    }
    else
    {
        typeFade.request (type);
    }
}

float AutoFilter::renderFilter (int ch, float x, int type)
{
    auto& a = stageA[(size_t) ch];
    auto& b = stageB[(size_t) ch];
    const auto o = a.tick (x);

    switch (type)
    {
        case lowpass:    return o.low * passGain;
        case lowpass24:  return b.tick (o.low).low * passGain;
        case bandpass:   return o.band * bandGain;
        case highpass:   return o.high * passGain;
        case highpass24: return b.tick (o.high).high * passGain;
        case notch:
        default:         return o.low + o.high;
    }
}

void AutoFilter::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2 || numSamples <= 0)
        return;

    float* channels[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };

    for (int start = 0; start < numSamples; start += fx::chunkSize)
    {
        const int len = juce::jmin (fx::chunkSize, numSamples - start);

        // Control rate: LFO, envelope and filter coefficients once per chunk.
        const double rate = lfoSync ? tempo / 60.0 / fx::lfoDivisionBeats (lfoDivision) : (double) lfoRateHz;
        const float spread = lfoSpread.skip (len);
        const float lfoDepth = lfoAmount.skip (len);
        const float envDepth = envAmount.skip (len);
        const float res = resonance.skip (len);
        const float baseOct = cutoffOct.skip (len);
        const float drive = driveDb.skip (len);
        const float driveGain = fx::dbToGain (drive);
        const float driveAmount = juce::jmin (1.0f, drive / 3.0f);

        for (int ch = 0; ch < 2; ++ch)
        {
            const float phase = fx::wrapPhase ((float) lfoPhase + (ch == 1 ? spread * 0.5f : 0.0f));
            if (phase < lastChannelPhase[(size_t) ch])
                randomHeld[(size_t) ch] = noise.next();
            lastChannelPhase[(size_t) ch] = phase;

            const float raw = lfoShape == 5 ? randomHeld[(size_t) ch] : fx::lfoShape (lfoShape, phase);
            auto& smooth = lfoSmooth[(size_t) ch];
            smooth += lfoSmoothCoeff * (raw - smooth);

            const float octaves = baseOct + smooth * lfoDepth * 4.0f + envDepth * juce::jmin (2.0f, envLevel) * 5.0f;
            const float hz = std::exp2 (juce::jlimit (4.0f, 14.4f, octaves));
            stageA[(size_t) ch].set (hz, res * 0.95f);
            stageB[(size_t) ch].set (hz, 0.3f);
        }

        bandGain = stageA[0].getK();
        passGain = 1.0f / (1.0f + 1.5f * res * res * res);
        lfoPhase += rate * len / sampleRate;
        lfoPhase -= std::floor (lfoPhase);

        for (int i = start; i < start + len; ++i)
        {
            bool switched = false;
            const float typeGain = typeFade.next (switched);
            if (switched) // stage A is shared by all types; stage B gets a different input now
                for (auto& f : stageB)
                    f.reset();

            const float inL = channels[0][i], inR = channels[1][i];
            const float level = juce::jmax (std::abs (inL), std::abs (inR));
            envLevel += (level > envLevel ? envAttack : envRelease) * (level - envLevel);

            const float m = mix.next();
            const int type = typeFade.mode();

            for (int ch = 0; ch < 2; ++ch)
            {
                const float dry = channels[ch][i];
                float x = dry;
                if (driveAmount > 0.0f)
                    x += driveAmount * (fx::fastTanh (x * driveGain) - x);

                float wet = renderFilter (ch, x, type);
                wet = dry + typeGain * (wet - dry);
                channels[ch][i] = fx::mixDryWet (dry, wet, m);
            }
        }
    }
}

} // namespace rb
