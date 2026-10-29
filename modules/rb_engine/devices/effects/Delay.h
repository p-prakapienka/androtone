#pragma once

#include "FxDsp.h"

namespace rb
{

// Delay (DEV-22): stereo or ping-pong echo, free (seconds) or tempo-synced (note divisions),
// band-limiting filter in the feedback path and a slow time wobble. Time changes crossfade
// between two read taps, so dragging the time never clicks or zips.
class Delay : public EffectProcessor
{
public:
    static DeviceSpec createSpec();

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void setParameters (const float* values, int numValues) override;
    void process (juce::AudioBuffer<float>& buffer, int numSamples) override;
    void setTempo (double bpm) override { tempo = fx::clampTempo (bpm); }

    static constexpr double maxDelaySeconds = 4.0;

private:
    enum Param { pSync, pDivision, pTime, pFeedback, pPingPong, pFilterFreq, pFilterWidth, pModulation, pMix, numParams };

    float targetDelaySamples() const;
    void updateFilters();

    double sampleRate = 48000.0, tempo = 120.0;
    bool snapNext = true, snapDelay = true;

    bool sync = true, pingPong = false;
    int division = 5;
    float timeSeconds = 0.25f;

    fx::Smoothed feedback, filterOct, filterWidth, modulation, mix;
    std::array<fx::DelayLine, 2> lines;
    std::array<dsp::Svf, 2> highPass, lowPass;

    float currentDelay = 1000.0f, nextDelay = 1000.0f, fade = 0.0f, fadeStep = 0.001f;
    bool fading = false;

    double modPhase = 0.0;
    std::array<float, 2> modOffset {};
};

} // namespace rb
