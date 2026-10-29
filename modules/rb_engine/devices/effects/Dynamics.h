#pragma once

#include "FxDsp.h"

namespace rb
{

// Dynamics (DEV-22): feed-forward compressor with a stereo-linked peak detector, 6 dB soft
// knee and log-domain attack/release, optional automatic makeup gain, followed by low and high
// shelving EQ and an output gain.
class Dynamics : public EffectProcessor
{
public:
    static DeviceSpec createSpec();

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void setParameters (const float* values, int numValues) override;
    void process (juce::AudioBuffer<float>& buffer, int numSamples) override;

private:
    enum Param { pThreshold, pRatio, pAttack, pRelease, pMakeup, pLow, pHigh, pOutput, numParams };
    static constexpr float kneeDb = 6.0f;

    void updateEq();

    double sampleRate = 48000.0;
    bool snapNext = true;

    fx::Smoothed threshold, ratio, makeup, low, high, output;
    float attackCoeff = 0.01f, releaseCoeff = 0.001f;
    float envelopeDb = 0.0f;

    std::array<fx::EqCoeffs, 2> eqCoeffs, eqTargets, eqSteps; // per-sample coefficient ramps
    std::array<std::array<fx::EqFilter, 2>, 2> eq;
};

} // namespace rb
