#pragma once

#include "../Parameter.h"
#include "../dsp/Biquad.h"

#include <juce_audio_basics/juce_audio_basics.h>

// One mixer channel: 3-band EQ, stereo width, pan, volume, mute/solo, and post-fader sends to the
// master delay and reverb. Created alongside its machine and owned by the engine's rack slot.
class ChannelStrip {
public:
    enum ParameterIndex { volume, pan, width, eqLow, eqMid, eqHigh, delaySend, reverbSend, mute, solo, numParameters };

    static constexpr double lowShelfHz = 250.0;
    static constexpr double midPeakHz = 1200.0;
    static constexpr double highShelfHz = 4000.0;

    ChannelStrip();

    void prepare(double sampleRate);

    // Processes the machine's stereo output in place. `audible` is false when the channel is
    // muted, or another channel is soloed; the channel then fades to silence (sends included).
    void process(juce::AudioBuffer<float>& buffer, int numSamples, bool audible);

    // Adds this channel's (already processed) signal to the send buses.
    void addSends(const juce::AudioBuffer<float>& buffer, juce::AudioBuffer<float>& delayBus,
                  juce::AudioBuffer<float>& reverbBus, int numSamples);

    bool isMuted() const { return parameters.get(mute) >= 0.5f; }
    bool isSoloed() const { return parameters.get(solo) >= 0.5f; }

    ParameterSet& getParameters() { return parameters; }
    const ParameterSet& getParameters() const { return parameters; }

private:
    ParameterSet parameters;
    double sampleRate = 48000.0;

    Biquad low;
    Biquad mid;
    Biquad high;
    float lowGainDb = 0.0f;
    float midGainDb = 0.0f;
    float highGainDb = 0.0f;

    // Last block's gains, ramped towards this block's to avoid zipper noise. Not primed until the
    // first block after prepare(): that block starts from its own values instead of stale ones.
    bool primed = false;
    float lastLeftGain = 0.0f;
    float lastRightGain = 0.0f;
    float lastDelaySend = 0.0f;
    float lastReverbSend = 0.0f;
    float currentAudibility = 1.0f;
    float lastAudibility = 1.0f;

    void updateEq();
};
