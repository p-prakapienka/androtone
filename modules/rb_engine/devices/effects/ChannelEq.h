#pragma once

#include "FxDsp.h"

namespace rb
{

// EQ (DEV-22): 3-band channel EQ with a low cut. Low shelf at 120 Hz, a sweepable mid peak,
// high shelf at 6 kHz, 12 dB/oct low cut and an output gain. RBJ biquads.
class ChannelEq : public EffectProcessor
{
public:
    static DeviceSpec createSpec();

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void setParameters (const float* values, int numValues) override;
    void process (juce::AudioBuffer<float>& buffer, int numSamples) override;

private:
    enum Param { pLowCut, pLow, pMid, pMidFreq, pHigh, pOutput, numParams };
    static constexpr int numBands = 4;

    void updateCoefficients();

    double sampleRate = 48000.0;
    bool snapNext = true;
    fx::Smoothed lowCut, low, mid, midFreq, high, output;
    // coeffs ramp per sample towards targets (recomputed per chunk while parameters move).
    std::array<fx::EqCoeffs, numBands> coeffs, targets, steps;
    std::array<std::array<fx::EqFilter, numBands>, 2> filters;
};

} // namespace rb
