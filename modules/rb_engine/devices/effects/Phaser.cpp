namespace rb
{

DeviceSpec Phaser::createSpec()
{
    DeviceSpec spec;
    spec.typeId = "phaser";
    spec.displayName = "Phaser";

    auto& p = spec.params;
    p.push_back (ParamSpec::choice ("mode", "Mode", { "Phaser", "Flanger", "Doubler" }, 0));
    p.push_back (ParamSpec::continuous ("rate", "Rate", 0.02f, 10.0f, 0.3f, ParamUnit::hertz, 1.0f));
    p.push_back (ParamSpec::continuous ("amount", "Amount", 0.0f, 1.0f, 0.6f, ParamUnit::percent));
    p.push_back (ParamSpec::continuous ("feedback", "Feedback", -0.9f, 0.9f, 0.4f, ParamUnit::percent));
    p.push_back (ParamSpec::continuous ("frequency", "Frequency", 100.0f, 8000.0f, 700.0f, ParamUnit::hertz, 1000.0f));
    p.push_back (ParamSpec::continuous ("time", "Time", 0.0005f, 0.02f, 0.003f, ParamUnit::seconds, 0.004f));
    p.push_back (ParamSpec::continuous ("spread", "Spread", 0.0f, 1.0f, 0.5f, ParamUnit::percent));
    p.push_back (ParamSpec::continuous ("mix", "Dry/Wet", 0.0f, 1.0f, 0.5f, ParamUnit::percent));
    return spec;
}

void Phaser::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    for (auto* s : { &amount, &feedback, &spread, &mix })
        s->prepare (sampleRate, 0.01);
    frequencyOct.prepare (sampleRate, 0.03);
    timeMs.prepare (sampleRate, 0.05);
    modeFade.prepare (sampleRate);
    for (auto& line : lines)
        line.prepare ((int) std::ceil (sampleRate * 0.045));
    snapNext = true;
    reset();
}

void Phaser::clearState()
{
    for (auto& ch : allpassState)
        ch.fill (0.0f);
    lastOut = {};
}

void Phaser::reset()
{
    clearState();
    for (auto& line : lines)
        line.reset();
    phase = 0.0;
    snapControls = true;
}

void Phaser::setParameters (const float* values, int numValues)
{
    const auto get = [&] (int index, float fallback) { return fx::param (values, numValues, index, fallback); };

    const int mode = fx::choice (get (pMode, 0.0f), 3);
    rateHz = juce::jlimit (0.02f, 10.0f, get (pRate, 0.3f));
    amount.setTarget (juce::jlimit (0.0f, 1.0f, get (pAmount, 0.6f)));
    feedback.setTarget (juce::jlimit (-0.9f, 0.9f, get (pFeedback, 0.4f)));
    frequencyOct.setTarget (std::log2 (juce::jlimit (100.0f, 8000.0f, get (pFrequency, 700.0f))));
    timeMs.setTarget (1000.0f * juce::jlimit (0.0005f, 0.02f, get (pTime, 0.003f)));
    spread.setTarget (juce::jlimit (0.0f, 1.0f, get (pSpread, 0.5f)));
    mix.setTarget (juce::jlimit (0.0f, 1.0f, get (pMix, 0.5f)));

    if (snapNext)
    {
        for (auto* s : { &amount, &feedback, &frequencyOct, &timeMs, &spread, &mix })
            s->snapToTarget();
        modeFade.snap (mode);
        snapControls = true;
        snapNext = false;
    }
    else
    {
        modeFade.request (mode);
    }
}

void Phaser::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2 || numSamples <= 0)
        return;

    float* channels[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };
    const float msToSamples = 0.001f * (float) sampleRate;

    for (int start = 0; start < numSamples; start += fx::chunkSize)
    {
        const int len = juce::jmin (fx::chunkSize, numSamples - start);
        const int mode = modeFade.mode();

        phase += rateHz * len / sampleRate;
        phase -= std::floor (phase);

        const float depth = amount.skip (len);
        const float centreOct = frequencyOct.skip (len);
        const float time = timeMs.skip (len);
        const float stereo = spread.skip (len);

        // Control values at the end of the chunk, ramped per sample (snapped after a mode switch).
        std::array<float, 2> coeffStep {}, delayStep {};
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const float lfo = std::sin (fx::twoPi * fx::wrapPhase ((float) phase + (ch == 1 ? 0.5f * stereo : 0.0f)));
            if (mode == phaser)
            {
                const float hz = std::exp2 (juce::jlimit (4.3f, 14.2f, centreOct + depth * 2.5f * lfo));
                const float t = std::tan (fx::pi * juce::jmin (hz, 0.45f * (float) sampleRate) / (float) sampleRate);
                const float target = (t - 1.0f) / (t + 1.0f);
                if (snapControls)
                    coeff[ch] = target;
                coeffStep[ch] = (target - coeff[ch]) / (float) len;
            }
            else
            {
                float ms = mode == flanger ? time * (1.0f + 0.9f * depth * lfo)
                                           : 10.0f + time + depth * 1.5f * lfo + (ch == 1 ? 4.0f * stereo : 0.0f);
                const float target = juce::jmax (0.05f, ms) * msToSamples;
                if (snapControls)
                    delaySamples[ch] = target;
                delayStep[ch] = (target - delaySamples[ch]) / (float) len;
            }
        }
        snapControls = false;

        for (int i = start; i < start + len; ++i)
        {
            bool switched = false;
            const float modeGain = modeFade.next (switched);
            if (switched)
            {
                clearState();
                snapControls = true;
            }

            const float fb = feedback.next();
            const float m = mix.next();
            const float wetGain = 1.0f - 0.5f * std::abs (fb);

            for (size_t ch = 0; ch < 2; ++ch)
            {
                const float dry = channels[ch][i];
                float wet = 0.0f;

                if (mode == phaser)
                {
                    coeff[ch] += coeffStep[ch];
                    const float a = coeff[ch];
                    float x = dry + fb * lastOut[ch];
                    for (auto& s : allpassState[ch])
                    {
                        const float y = a * x + s;
                        s = x - a * y;
                        x = y;
                    }
                    lastOut[ch] = x;
                    wet = x * wetGain;
                    lines[ch].write (dry); // keeps the delay modes ready for a switch
                }
                else
                {
                    delaySamples[ch] += delayStep[ch];
                    auto& line = lines[ch];
                    const float tap = line.readCubic (delaySamples[ch]);
                    // Feedback follows the mode fade so the line never holds a step in content
                    // that a later mode would read.
                    line.write (mode == flanger ? dry + fx::softLimit (fb * modeGain * tap) : dry);
                    wet = mode == flanger ? tap * wetGain : tap;
                }

                wet = dry + modeGain * (wet - dry);
                channels[ch][i] = fx::mixDryWet (dry, wet, m);
            }
        }
    }
}

} // namespace rb
