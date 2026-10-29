#pragma once

#include "FxDsp.h"

namespace rb
{

// Chorus (DEV-22): modulated delay lines in three modes.
// - Chorus: one voice per channel, quadrature LFOs across the stereo field.
// - Ensemble: three voices per channel at 120 degree LFO offsets plus a faster shimmer LFO.
// - Vibrato: one short voice; use 100 % wet for pure pitch modulation.
class Chorus : public EffectProcessor
{
public:
    static DeviceSpec createSpec();

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void setParameters (const float* values, int numValues) override;
    void process (juce::AudioBuffer<float>& buffer, int numSamples) override;

private:
    enum Param { pMode, pRate, pAmount, pFeedback, pWidth, pMix, numParams };
    enum Mode { chorus, ensemble, vibrato };
    static constexpr int numVoices = 3;

    double sampleRate = 48000.0;
    bool snapNext = true;
    float rateHz = 0.6f;

    fx::Smoothed baseMs, depthMs, feedback, width, mix, shimmer;
    std::array<fx::Smoothed, numVoices> voiceGain;
    std::array<fx::DelayLine, 2> lines;
    std::array<std::array<float, numVoices>, 2> lastDelay {};
    double phase = 0.0, fastPhase = 0.0;
};

} // namespace rb
