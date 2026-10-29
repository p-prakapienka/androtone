#pragma once

#include "FxDsp.h"

namespace rb
{

// Crusher (DEV-22): bit depth reduction (continuous 1..16 bits) and sample-rate reduction
// (sample and hold with optional timing jitter), with a resonant low pass placed before or
// after the crusher, or switched off.
class Crusher : public EffectProcessor
{
public:
    static DeviceSpec createSpec();

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void setParameters (const float* values, int numValues) override;
    void process (juce::AudioBuffer<float>& buffer, int numSamples) override;

private:
    enum Param { pBits, pRate, pJitter, pFilter, pCutoff, pMix, numParams };
    enum FilterMode { filterOff, filterPre, filterPost };

    double sampleRate = 48000.0;
    bool snapNext = true;

    fx::Smoothed bits, rate, jitter, cutoffOct, mix;
    fx::ModeFade filterFade;
    std::array<dsp::Svf, 2> filters;

    double holdPhase = 0.0, holdLength = 1.0;
    std::array<float, 2> held {};
    fx::Noise noise { 0xc0ffeeu };
};

} // namespace rb
