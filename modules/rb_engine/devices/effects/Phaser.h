#pragma once

#include "FxDsp.h"

namespace rb
{

// Phaser (DEV-22) with three modes:
// - Phaser: six swept first-order allpass stages with feedback around a centre frequency.
// - Flanger: short swept delay (Time) with feedback (comb filter).
// - Doubler: 10 ms + Time delay with gentle modulation and a stereo offset (Spread).
// The LFO of the right channel is offset by up to 180 degrees (Spread). Mode changes fade
// through dry so they never click.
class Phaser : public EffectProcessor
{
public:
    static DeviceSpec createSpec();

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void setParameters (const float* values, int numValues) override;
    void process (juce::AudioBuffer<float>& buffer, int numSamples) override;

private:
    enum Param { pMode, pRate, pAmount, pFeedback, pFrequency, pTime, pSpread, pMix, numParams };
    enum Mode { phaser, flanger, doubler };
    static constexpr int numStages = 6;

    void clearState();

    double sampleRate = 48000.0;
    bool snapNext = true, snapControls = true;
    float rateHz = 0.3f;

    fx::Smoothed amount, feedback, frequencyOct, timeMs, spread, mix;
    fx::ModeFade modeFade;

    double phase = 0.0;
    std::array<std::array<float, numStages>, 2> allpassState {};
    std::array<float, 2> coeff {}, lastOut {}, delaySamples {};
    std::array<fx::DelayLine, 2> lines;
};

} // namespace rb
