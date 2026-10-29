#pragma once

#include "../Parameter.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

// The master section: send returns for a tempo-synced stereo delay and a reverb, master volume,
// and a peak limiter that never lets the output exceed its ceiling.
class MasterBus {
public:
    enum ParameterIndex {
        volume, delayBeats, delayFeedback, delayDamping, reverbSize, reverbDamping, reverbWidth, limiterCeiling,
        numParameters
    };

    static constexpr double maxDelaySeconds = 4.0;

    MasterBus();

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    // `mix` (stereo) holds the summed channels; the buses hold the summed sends. Adds the delay and
    // reverb returns to `mix`, then applies volume and the limiter, in place.
    void process(juce::AudioBuffer<float>& mix, juce::AudioBuffer<float>& delayBus, juce::AudioBuffer<float>& reverbBus,
                 int numSamples, double samplesPerBeat);

    ParameterSet& getParameters() { return parameters; }
    const ParameterSet& getParameters() const { return parameters; }

private:
    ParameterSet parameters;
    double sampleRate = 48000.0;

    juce::AudioBuffer<float> delayLine;
    int delayWrite = 0;
    float dampingState[2] {};

    juce::Reverb reverb;

    float limiterGain = 1.0f;
    float lastVolume = 0.8f;
    bool primed = false; // see ChannelStrip
};
