#pragma once

#include "FxDsp.h"

namespace rb
{

// Reverb (DEV-22): algorithmic stereo reverb.
//   pre-delay -> low cut -> 4 allpass diffusers per channel -> 8-line feedback delay network
//   (Hadamard mixing, per-line decay gains from RT60, one-pole damping, slowly modulated
//   read positions against metallic ringing) -> decorrelated stereo taps -> width.
class Reverb : public EffectProcessor
{
public:
    static DeviceSpec createSpec();

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void setParameters (const float* values, int numValues) override;
    void process (juce::AudioBuffer<float>& buffer, int numSamples) override;

    static constexpr double maxPreDelaySeconds = 0.25;

private:
    enum Param { pPreDelay, pSize, pDecay, pDiffusion, pHighCut, pLowCut, pWidth, pMix, numParams };
    static constexpr int numLines = 8;
    static constexpr int numDiffusers = 4;

    void updateLoop();

    double sampleRate = 48000.0;
    bool snapNext = true;

    fx::Smoothed preDelay, size, decay, diffusion, highCutOct, lowCutOct, width, mix;

    std::array<fx::DelayLine, 2> preDelayLines;
    std::array<std::array<fx::DelayLine, numDiffusers>, 2> diffusers;
    std::array<std::array<int, numDiffusers>, 2> diffuserDelays {};
    std::array<fx::DelayLine, numLines> lines;

    // lineTarget (size) is recomputed per chunk; the read position ramps per sample towards
    // lineTarget + modulation, so size changes glide without zipper noise.
    std::array<float, numLines> lineTarget {}, lineDamp {}, readDelay {}, readStep {};
    std::array<float, numLines> lineGain {}, gainTarget {}, gainStep {};
    std::array<double, numLines> modPhase {};
    float dampCoeff = 0.5f, lowCutCoeff = 0.01f;
    std::array<float, 2> lowCutState {};
    float modDepth = 8.0f;
};

} // namespace rb
