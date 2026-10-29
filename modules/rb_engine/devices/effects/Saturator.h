#pragma once

#include "FxDsp.h"

namespace rb
{

// Saturator (DEV-22): waveshaper with six curves (Soft, Analog, Tape, Hard, Fold, Bit), drive,
// a pre-shaper tilt EQ (Colour), output gain and dry/wet. The shaper runs at 2x oversampling
// (polyphase IIR half-band filters) to keep aliasing down; curve changes crossfade.
class Saturator : public EffectProcessor
{
public:
    static DeviceSpec createSpec();

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void setParameters (const float* values, int numValues) override;
    void process (juce::AudioBuffer<float>& buffer, int numSamples) override;

private:
    enum Param { pCurve, pDrive, pColour, pOutput, pMix, numParams };
    enum Curve { soft, analog, tape, hard, fold, bit, numCurves };

    static float shape (int curve, float x);
    void processChunk (float* const* channels, int numSamples);

    double sampleRate = 48000.0;
    int maxBlock = 512;
    bool snapNext = true;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::AudioBuffer<float> dryBuffer;

    fx::Smoothed driveDb, colour, outputDb, mix;
    int curve = analog, previousCurve = analog;
    float curveFade = 1.0f, curveFadeStep = 0.01f;

    float tiltCoeff = 0.1f, dcCoeff = 0.999f;
    std::array<float, 2> tiltState {}, dcInput {}, dcOutput {};
};

} // namespace rb
