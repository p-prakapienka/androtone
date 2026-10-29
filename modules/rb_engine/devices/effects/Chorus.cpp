namespace rb
{

DeviceSpec Chorus::createSpec()
{
    DeviceSpec spec;
    spec.typeId = "chorus";
    spec.displayName = "Chorus";

    auto& p = spec.params;
    p.push_back (ParamSpec::choice ("mode", "Mode", { "Chorus", "Ensemble", "Vibrato" }, 0));
    p.push_back (ParamSpec::continuous ("rate", "Rate", 0.05f, 10.0f, 0.6f, ParamUnit::hertz, 1.0f));
    p.push_back (ParamSpec::continuous ("amount", "Amount", 0.0f, 1.0f, 0.5f, ParamUnit::percent));
    p.push_back (ParamSpec::continuous ("feedback", "Feedback", 0.0f, 0.9f, 0.0f, ParamUnit::percent));
    p.push_back (ParamSpec::continuous ("width", "Width", 0.0f, 1.0f, 1.0f, ParamUnit::percent));
    p.push_back (ParamSpec::continuous ("mix", "Dry/Wet", 0.0f, 1.0f, 0.5f, ParamUnit::percent));
    return spec;
}

void Chorus::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    for (auto* s : { &baseMs, &depthMs, &shimmer })
        s->prepare (sampleRate, 0.08);
    for (auto* s : { &feedback, &width, &mix })
        s->prepare (sampleRate, 0.01);
    for (auto& g : voiceGain)
        g.prepare (sampleRate, 0.03);

    for (auto& line : lines)
        line.prepare ((int) std::ceil (sampleRate * 0.06));
    snapNext = true;
    reset();
}

void Chorus::reset()
{
    for (auto& line : lines)
        line.reset();
    phase = fastPhase = 0.0;
    const float base = baseMs.get() * 0.001f * (float) sampleRate;
    for (auto& ch : lastDelay)
        ch.fill (base);
}

void Chorus::setParameters (const float* values, int numValues)
{
    const auto get = [&] (int index, float fallback) { return fx::param (values, numValues, index, fallback); };

    const int mode = fx::choice (get (pMode, 0.0f), 3);
    rateHz = juce::jlimit (0.05f, 10.0f, get (pRate, 0.6f));
    const float amount = juce::jlimit (0.0f, 1.0f, get (pAmount, 0.5f));

    static constexpr float bases[] = { 14.0f, 9.0f, 4.0f };
    static constexpr float depths[] = { 6.0f, 3.0f, 3.5f };
    baseMs.setTarget (bases[mode]);
    depthMs.setTarget (depths[mode] * amount);
    shimmer.setTarget (mode == ensemble ? 0.25f : 0.0f);
    feedback.setTarget (mode == vibrato ? 0.0f : juce::jlimit (0.0f, 0.9f, get (pFeedback, 0.0f)));
    width.setTarget (juce::jlimit (0.0f, 1.0f, get (pWidth, 1.0f)));
    mix.setTarget (juce::jlimit (0.0f, 1.0f, get (pMix, 0.5f)));

    voiceGain[0].setTarget (mode == ensemble ? 0.62f : 1.0f);
    voiceGain[1].setTarget (mode == ensemble ? 0.62f : 0.0f);
    voiceGain[2].setTarget (mode == ensemble ? 0.62f : 0.0f);

    if (snapNext)
    {
        for (auto* s : { &baseMs, &depthMs, &shimmer, &feedback, &width, &mix })
            s->snapToTarget();
        for (auto& g : voiceGain)
            g.snapToTarget();
        const float base = baseMs.get() * 0.001f * (float) sampleRate;
        for (auto& ch : lastDelay)
            ch.fill (base);
        snapNext = false;
    }
}

void Chorus::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2 || numSamples <= 0)
        return;

    float* channels[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };
    const float msToSamples = 0.001f * (float) sampleRate;
    static constexpr float voiceBase[] = { 1.0f, 1.3f, 0.75f };

    for (int start = 0; start < numSamples; start += fx::chunkSize)
    {
        const int len = juce::jmin (fx::chunkSize, numSamples - start);

        // Target delays at the end of this chunk; per-sample values are interpolated.
        phase += rateHz * len / sampleRate;
        phase -= std::floor (phase);
        fastPhase += rateHz * 7.1 * len / sampleRate;
        fastPhase -= std::floor (fastPhase);

        const float base = baseMs.skip (len);
        const float depth = depthMs.skip (len);
        const float fast = shimmer.skip (len);
        const float stereoOffset = 0.25f * width.getTarget();

        std::array<std::array<float, numVoices>, 2> target {};
        std::array<std::array<float, numVoices>, 2> step {};
        for (int ch = 0; ch < 2; ++ch)
            for (int v = 0; v < numVoices; ++v)
            {
                const float offset = (float) v / 3.0f + (ch == 1 ? stereoOffset : 0.0f);
                const float slow = std::sin (fx::twoPi * fx::wrapPhase ((float) phase + offset));
                const float quick = std::sin (fx::twoPi * fx::wrapPhase ((float) fastPhase + offset));
                const float ms = base * voiceBase[v] + depth * (slow + fast * quick);
                const auto c = (size_t) ch, vi = (size_t) v;
                target[c][vi] = juce::jmax (1.0f, ms) * msToSamples;
                step[c][vi] = (target[c][vi] - lastDelay[c][vi]) / (float) len;
            }

        for (int i = start; i < start + len; ++i)
        {
            const float fb = feedback.next();
            const float w = width.next();
            const float m = mix.next();
            const float g0 = voiceGain[0].next(), g1 = voiceGain[1].next(), g2 = voiceGain[2].next();

            float wet[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& d = lastDelay[(size_t) ch];
                auto& line = lines[(size_t) ch];
                for (size_t v = 0; v < (size_t) numVoices; ++v)
                    d[v] += step[(size_t) ch][v];

                const float v0 = line.readCubic (d[0]);
                float sum = g0 * v0;
                if (g1 > 0.0f) sum += g1 * line.readCubic (d[1]);
                if (g2 > 0.0f) sum += g2 * line.readCubic (d[2]);

                line.write (channels[ch][i] + fx::softLimit (fb * v0));
                wet[ch] = sum;
            }

            const float midSig = 0.5f * (wet[0] + wet[1]);
            const float side = 0.5f * (wet[0] - wet[1]) * w;
            channels[0][i] = fx::mixDryWet (channels[0][i], midSig + side, m);
            channels[1][i] = fx::mixDryWet (channels[1][i], midSig - side, m);
        }

        lastDelay = target; // removes accumulated rounding
    }
}

} // namespace rb
