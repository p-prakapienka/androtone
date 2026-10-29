#pragma once

#include "FxDsp.h"

namespace rb
{

// Auto Filter (DEV-22): resonant state-variable filter (12/24 dB low/high pass, band pass,
// notch) with input drive, an LFO (free or tempo synced, stereo spread) and an envelope
// follower modulating the cutoff.
class AutoFilter : public EffectProcessor
{
public:
    static DeviceSpec createSpec();

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void setParameters (const float* values, int numValues) override;
    void process (juce::AudioBuffer<float>& buffer, int numSamples) override;
    void setTempo (double bpm) override { tempo = fx::clampTempo (bpm); }

private:
    enum Param { pType, pCutoff, pResonance, pDrive, pEnvelope, pLfoAmount, pLfoSync, pLfoRate,
                 pLfoDivision, pLfoShape, pLfoSpread, pMix, numParams };
    enum Type { lowpass, lowpass24, bandpass, highpass, highpass24, notch, numTypes };

    float renderFilter (int channel, float x, int type);

    double sampleRate = 48000.0, tempo = 120.0;
    bool snapNext = true;

    float lfoRateHz = 1.0f;
    bool lfoSync = false;
    int lfoDivision = 5, lfoShape = 0;

    fx::Smoothed cutoffOct, resonance, driveDb, envAmount, lfoAmount, lfoSpread, mix;
    fx::ModeFade typeFade;

    std::array<dsp::Svf, 2> stageA, stageB;
    float bandGain = 1.0f, passGain = 1.0f;

    double lfoPhase = 0.0;
    std::array<float, 2> lastChannelPhase {}, randomHeld {}, lfoSmooth {};
    float lfoSmoothCoeff = 0.1f;
    fx::Noise noise { 0x1234567u };

    float envLevel = 0.0f, envAttack = 0.1f, envRelease = 0.001f;
};

} // namespace rb
