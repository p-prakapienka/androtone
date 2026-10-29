namespace rb
{

DeviceSpec Delay::createSpec()
{
    DeviceSpec spec;
    spec.typeId = "delay";
    spec.displayName = "Delay";
    spec.groups = { "Delay", "Tone" };

    auto& p = spec.params;
    p.push_back (ParamSpec::toggle ("sync", "Sync", true, "Delay"));
    p.push_back (ParamSpec::choice ("division", "Beats", fx::delayDivisionNames(), 5, "Delay"));
    p.push_back (ParamSpec::continuous ("time", "Time", 0.001f, (float) maxDelaySeconds, 0.25f, ParamUnit::seconds, 0.3f, "Delay"));
    p.push_back (ParamSpec::continuous ("feedback", "Feedback", 0.0f, 0.95f, 0.4f, ParamUnit::percent, 0.0f, "Delay"));
    p.push_back (ParamSpec::toggle ("pingPong", "Ping Pong", false, "Delay"));
    p.push_back (ParamSpec::continuous ("filterFreq", "Filter", 50.0f, 18000.0f, 1000.0f, ParamUnit::hertz, 1000.0f, "Tone"));
    p.push_back (ParamSpec::continuous ("filterWidth", "Width", 0.0f, 1.0f, 0.75f, ParamUnit::percent, 0.0f, "Tone"));
    p.push_back (ParamSpec::continuous ("modulation", "Modulation", 0.0f, 1.0f, 0.1f, ParamUnit::percent, 0.0f, "Tone"));
    p.push_back (ParamSpec::continuous ("mix", "Dry/Wet", 0.0f, 1.0f, 0.35f, ParamUnit::percent, 0.0f, "Delay"));
    return spec;
}

void Delay::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    for (auto* s : { &feedback, &filterWidth, &modulation, &mix })
        s->prepare (sampleRate, 0.01);
    filterOct.prepare (sampleRate, 0.03);

    for (auto& line : lines)
        line.prepare ((int) std::ceil (sampleRate * (maxDelaySeconds + 0.05)));
    for (auto* f : { &highPass[0], &highPass[1], &lowPass[0], &lowPass[1] })
        f->prepare (sampleRate);

    fadeStep = (float) (1.0 / (0.05 * sampleRate));
    snapNext = true;
    reset();
}

void Delay::reset()
{
    for (auto& line : lines)
        line.reset();
    for (auto* f : { &highPass[0], &highPass[1], &lowPass[0], &lowPass[1] })
        f->reset();
    modPhase = 0.0;
    modOffset = {};
    fading = false;
    snapDelay = true;
}

void Delay::setParameters (const float* values, int numValues)
{
    const auto get = [&] (int index, float fallback) { return fx::param (values, numValues, index, fallback); };

    sync = get (pSync, 1.0f) >= 0.5f;
    division = fx::choice (get (pDivision, 5.0f), fx::numDelayDivisions);
    timeSeconds = juce::jlimit (0.001f, (float) maxDelaySeconds, get (pTime, 0.25f));
    pingPong = get (pPingPong, 0.0f) >= 0.5f;
    feedback.setTarget (juce::jlimit (0.0f, 0.95f, get (pFeedback, 0.4f)));
    filterOct.setTarget (std::log2 (juce::jlimit (50.0f, 18000.0f, get (pFilterFreq, 1000.0f))));
    filterWidth.setTarget (juce::jlimit (0.0f, 1.0f, get (pFilterWidth, 0.75f)));
    modulation.setTarget (juce::jlimit (0.0f, 1.0f, get (pModulation, 0.1f)));
    mix.setTarget (juce::jlimit (0.0f, 1.0f, get (pMix, 0.35f)));

    if (snapNext)
    {
        for (auto* s : { &feedback, &filterOct, &filterWidth, &modulation, &mix })
            s->snapToTarget();
        updateFilters();
        snapNext = false;
    }
}

float Delay::targetDelaySamples() const
{
    const double seconds = sync ? fx::delayDivisionBeats (division) * 60.0 / tempo : (double) timeSeconds;
    return (float) (juce::jlimit (0.001, maxDelaySeconds, seconds) * sampleRate);
}

void Delay::updateFilters()
{
    // Band limit: high pass and low pass spaced `width` octaves around the filter frequency.
    const float centre = filterOct.get();
    const float halfWidth = 0.5f * (1.0f + 9.0f * filterWidth.get());
    const float lowHz = std::exp2 (juce::jlimit (4.0f, 14.5f, centre - halfWidth));
    const float highHz = std::exp2 (juce::jlimit (4.0f, 14.5f, centre + halfWidth));
    for (auto& f : highPass) f.set (lowHz, 0.0f);
    for (auto& f : lowPass)  f.set (highHz, 0.0f);
}

void Delay::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2 || numSamples <= 0)
        return;

    float* channels[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };

    const float target = targetDelaySamples();
    if (snapDelay)
    {
        currentDelay = nextDelay = target;
        fading = false;
        snapDelay = false;
    }

    for (int start = 0; start < numSamples; start += fx::chunkSize)
    {
        const int len = juce::jmin (fx::chunkSize, numSamples - start);

        if (filterOct.isSmoothing() || filterWidth.isSmoothing())
        {
            filterOct.skip (len);
            filterWidth.skip (len);
            updateFilters();
        }

        // Time wobble: slow sine, opposite phase per side, depth scaled to the delay time.
        modPhase += 0.6 * len / sampleRate;
        modPhase -= std::floor (modPhase);
        const float depth = modulation.skip (len) * juce::jmin (0.004f * (float) sampleRate, 0.05f * currentDelay);
        std::array<float, 2> modStep {};
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const float value = depth * std::sin (fx::twoPi * fx::wrapPhase ((float) modPhase + (ch == 1 ? 0.5f : 0.0f)));
            modStep[ch] = (value - modOffset[ch]) / (float) len;
        }

        for (int i = start; i < start + len; ++i)
        {
            if (! fading && std::abs (target - currentDelay) > 0.5f)
            {
                nextDelay = target;
                fade = 0.0f;
                fading = true;
            }

            const float fb = feedback.next();
            const float m = mix.next();

            float wet[2];
            for (size_t ch = 0; ch < 2; ++ch)
            {
                modOffset[ch] += modStep[ch];
                const auto& line = lines[ch];
                float tap = line.readCubic (currentDelay + modOffset[ch]);
                if (fading)
                    tap += fade * (line.readCubic (nextDelay + modOffset[ch]) - tap);
                wet[ch] = lowPass[ch].process (highPass[ch].process (tap, dsp::Svf::Mode::highpass), dsp::Svf::Mode::lowpass);
            }

            if (fading)
            {
                fade += fadeStep;
                if (fade >= 1.0f)
                {
                    currentDelay = nextDelay;
                    fading = false;
                }
            }

            const float inL = channels[0][i], inR = channels[1][i];
            if (pingPong)
            {
                lines[0].write (0.5f * (inL + inR) + fx::softLimit (fb * wet[1]));
                lines[1].write (fx::softLimit (fb * wet[0]));
            }
            else
            {
                lines[0].write (inL + fx::softLimit (fb * wet[0]));
                lines[1].write (inR + fx::softLimit (fb * wet[1]));
            }

            channels[0][i] = fx::mixDryWet (inL, wet[0], m);
            channels[1][i] = fx::mixDryWet (inR, wet[1], m);
        }
    }
}

} // namespace rb
