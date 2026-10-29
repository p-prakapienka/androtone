namespace rb
{

namespace
{
    // Mutually prime-ish line lengths (ms) at size scale 1, and LFO rates (Hz) per line.
    constexpr float reverbLineMs[] = { 29.3f, 33.7f, 38.9f, 43.1f, 48.7f, 53.9f, 59.1f, 66.7f };
    constexpr float reverbModHz[] = { 0.31f, 0.43f, 0.57f, 0.71f, 0.37f, 0.49f, 0.63f, 0.79f };
    constexpr float reverbEarlyFraction[] = { 0.13f, 0.29f, 0.21f, 0.37f, 0.17f, 0.33f, 0.25f, 0.41f };
    constexpr float reverbDiffuserMs[2][4] = { { 4.31f, 3.17f, 7.61f, 11.3f }, { 4.53f, 3.37f, 7.93f, 10.7f } };
    // Output tap signs: two orthogonal patterns give decorrelated left and right outputs.
    constexpr float reverbTapL[] = { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f };
    constexpr float reverbTapR[] = { 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f };
    constexpr float reverbMaxScale = 2.0f;

    float reverbSizeScale (float size) { return 0.3f + 1.7f * size; }

    // In-place 8-point fast Walsh-Hadamard transform, normalised (orthogonal).
    void hadamard8 (std::array<float, 8>& v)
    {
        for (size_t h = 1; h < 8; h *= 2)
            for (size_t i = 0; i < 8; i += h * 2)
                for (size_t j = i; j < i + h; ++j)
                {
                    const float a = v[j], b = v[j + h];
                    v[j] = a + b;
                    v[j + h] = a - b;
                }
        constexpr float scale = 0.35355339f; // 1 / sqrt (8)
        for (auto& x : v)
            x *= scale;
    }
}

DeviceSpec Reverb::createSpec()
{
    DeviceSpec spec;
    spec.typeId = "reverb";
    spec.displayName = "Reverb";

    auto& p = spec.params;
    p.push_back (ParamSpec::continuous ("preDelay", "Pre-Delay", 0.0f, (float) maxPreDelaySeconds, 0.015f, ParamUnit::seconds, 0.04f));
    p.push_back (ParamSpec::continuous ("size", "Size", 0.0f, 1.0f, 0.5f, ParamUnit::percent));
    p.push_back (ParamSpec::continuous ("decay", "Decay", 0.2f, 20.0f, 2.0f, ParamUnit::seconds, 2.5f));
    p.push_back (ParamSpec::continuous ("diffusion", "Diffusion", 0.0f, 1.0f, 0.75f, ParamUnit::percent));
    p.push_back (ParamSpec::continuous ("highCut", "High Cut", 1000.0f, 20000.0f, 9000.0f, ParamUnit::hertz, 6000.0f));
    p.push_back (ParamSpec::continuous ("lowCut", "Low Cut", 20.0f, 1000.0f, 80.0f, ParamUnit::hertz, 150.0f));
    p.push_back (ParamSpec::continuous ("width", "Width", 0.0f, 1.0f, 1.0f, ParamUnit::percent));
    p.push_back (ParamSpec::continuous ("mix", "Dry/Wet", 0.0f, 1.0f, 0.3f, ParamUnit::percent));
    return spec;
}

void Reverb::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    for (auto* s : { &decay, &diffusion, &highCutOct, &lowCutOct, &width, &mix })
        s->prepare (sampleRate, 0.03);
    preDelay.prepare (sampleRate, 0.1);
    size.prepare (sampleRate, 0.3);

    const float msToSamples = 0.001f * (float) sampleRate;
    modDepth = 8.0f * (float) sampleRate / 48000.0f;

    for (auto& line : preDelayLines)
        line.prepare ((int) std::ceil (maxPreDelaySeconds * sampleRate) + 4);

    for (size_t ch = 0; ch < 2; ++ch)
        for (size_t d = 0; d < (size_t) numDiffusers; ++d)
        {
            diffuserDelays[ch][d] = juce::jmax (1, juce::roundToInt (reverbDiffuserMs[ch][d] * msToSamples));
            diffusers[ch][d].prepare (diffuserDelays[ch][d] + 1);
        }

    for (size_t i = 0; i < (size_t) numLines; ++i)
    {
        lines[i].prepare ((int) std::ceil (reverbLineMs[i] * reverbMaxScale * msToSamples + 2.0f * modDepth) + 4);
        modPhase[i] = (double) i / numLines;
    }

    snapNext = true;
    reset();
}

void Reverb::reset()
{
    for (auto& line : preDelayLines)
        line.reset();
    for (auto& ch : diffusers)
        for (auto& d : ch)
            d.reset();
    for (auto& line : lines)
        line.reset();
    lineDamp = {};
    lowCutState = {};
}

void Reverb::setParameters (const float* values, int numValues)
{
    const auto get = [&] (int index, float fallback) { return fx::param (values, numValues, index, fallback); };

    preDelay.setTarget (juce::jlimit (0.0f, (float) maxPreDelaySeconds, get (pPreDelay, 0.015f)) * (float) sampleRate);
    size.setTarget (juce::jlimit (0.0f, 1.0f, get (pSize, 0.5f)));
    decay.setTarget (juce::jlimit (0.2f, 20.0f, get (pDecay, 2.0f)));
    diffusion.setTarget (juce::jlimit (0.0f, 1.0f, get (pDiffusion, 0.75f)));
    highCutOct.setTarget (std::log2 (juce::jlimit (1000.0f, 20000.0f, get (pHighCut, 9000.0f))));
    lowCutOct.setTarget (std::log2 (juce::jlimit (20.0f, 1000.0f, get (pLowCut, 80.0f))));
    width.setTarget (juce::jlimit (0.0f, 1.0f, get (pWidth, 1.0f)));
    mix.setTarget (juce::jlimit (0.0f, 1.0f, get (pMix, 0.3f)));

    if (snapNext)
    {
        for (auto* s : { &preDelay, &size, &decay, &diffusion, &highCutOct, &lowCutOct, &width, &mix })
            s->snapToTarget();
        updateLoop();
        for (size_t l = 0; l < (size_t) numLines; ++l)
            readDelay[l] = lineTarget[l] + modDepth;
        lineGain = gainTarget;
        snapNext = false;
    }
}

void Reverb::updateLoop()
{
    const float scale = reverbSizeScale (size.get()) * 0.001f * (float) sampleRate;
    const float rt60 = decay.get();
    for (size_t i = 0; i < (size_t) numLines; ++i)
    {
        lineTarget[i] = reverbLineMs[i] * scale;
        // -60 dB after rt60 seconds: gain per pass of a line of this length.
        gainTarget[i] = std::pow (10.0f, -3.0f * lineTarget[i] / ((float) sampleRate * rt60));
    }
    dampCoeff = fx::onePoleCoeff (sampleRate, juce::jmin (std::exp2 (highCutOct.get()), 0.45f * (float) sampleRate));
    lowCutCoeff = fx::onePoleCoeff (sampleRate, std::exp2 (lowCutOct.get()));
}

void Reverb::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2 || numSamples <= 0)
        return;

    float* channels[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };
    constexpr float inputGain = 0.5f, outputGain = 0.42f, earlyGain = 0.5f;

    for (int start = 0; start < numSamples; start += fx::chunkSize)
    {
        const int len = juce::jmin (fx::chunkSize, numSamples - start);

        if (size.isSmoothing() || decay.isSmoothing() || highCutOct.isSmoothing() || lowCutOct.isSmoothing())
        {
            for (auto* s : { &size, &decay, &highCutOct, &lowCutOct })
                s->skip (len);
            updateLoop();
        }

        const float g = 0.72f * diffusion.skip (len);

        for (size_t l = 0; l < (size_t) numLines; ++l)
        {
            modPhase[l] += reverbModHz[l] * len / sampleRate;
            modPhase[l] -= std::floor (modPhase[l]);
            // Length (size) and modulation share one per-sample ramp of the read position.
            const float target = lineTarget[l] + modDepth * (1.0f + std::sin (fx::twoPi * (float) modPhase[l]));
            readStep[l] = (target - readDelay[l]) / (float) len;
            gainStep[l] = (gainTarget[l] - lineGain[l]) / (float) len;
        }

        for (int i = start; i < start + len; ++i)
        {
            const float pre = preDelay.next();
            const float w = width.next();
            const float m = mix.next();

            // Pre-delay, low cut and input diffusion per channel.
            float diffused[2];
            for (size_t ch = 0; ch < 2; ++ch)
            {
                auto& pd = preDelayLines[ch];
                pd.write (channels[ch][i]);
                float x = pre < 1.0f ? pd.at (1) : pd.readLinear (pre + 1.0f);

                lowCutState[ch] += lowCutCoeff * (x - lowCutState[ch]);
                x -= lowCutState[ch];

                for (size_t d = 0; d < (size_t) numDiffusers; ++d)
                {
                    auto& ap = diffusers[ch][d];
                    const float delayed = ap.at (diffuserDelays[ch][d]);
                    const float v = x + g * delayed;
                    ap.write (v);
                    x = delayed - g * v;
                }
                diffused[ch] = x * inputGain;
            }

            // Feedback delay network.
            std::array<float, numLines> out;
            for (size_t l = 0; l < (size_t) numLines; ++l)
            {
                readDelay[l] += readStep[l];
                lineGain[l] += gainStep[l];
                const float y = lines[l].readLinear (readDelay[l]);
                lineDamp[l] += dampCoeff * (y - lineDamp[l]);
                out[l] = lineDamp[l] * lineGain[l];
            }

            // Late taps at the line ends plus early taps part-way along each line, so the wet
            // signal starts right away instead of after the shortest line.
            float wetL = 0.0f, wetR = 0.0f;
            for (size_t l = 0; l < (size_t) numLines; ++l)
            {
                const float early = earlyGain * lines[l].readLinear (readDelay[l] * reverbEarlyFraction[l]);
                wetL += reverbTapL[l] * out[l] + reverbTapR[l] * early;
                wetR += reverbTapR[l] * out[l] - reverbTapL[l] * early;
            }

            auto feedback = out;
            hadamard8 (feedback);
            for (size_t l = 0; l < (size_t) numLines; ++l)
            {
                const float in = (l & 1) == 0 ? diffused[0] : diffused[1];
                lines[l].write (feedback[l] + ((l & 2) == 0 ? in : -in));
            }

            wetL *= outputGain;
            wetR *= outputGain;
            const float midSig = 0.5f * (wetL + wetR);
            const float side = 0.5f * (wetL - wetR) * w;
            channels[0][i] = fx::mixDryWet (channels[0][i], midSig + side, m);
            channels[1][i] = fx::mixDryWet (channels[1][i], midSig - side, m);
        }
    }
}

} // namespace rb
