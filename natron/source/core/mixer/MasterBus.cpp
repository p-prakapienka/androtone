#include "MasterBus.h"

#include <cmath>

MasterBus::MasterBus() {
    parameters.add({ "volume", "Volume", 0.0f, 1.5f, 0.8f });
    parameters.add({ "delayBeats", "Delay Time", 0.125f, 2.0f, 0.75f }); // beats: 0.75 = dotted 8th
    parameters.add({ "delayFeedback", "Feedback", 0.0f, 0.95f, 0.4f });
    parameters.add({ "delayDamping", "Delay Damp", 0.0f, 1.0f, 0.3f });
    parameters.add({ "reverbSize", "Room Size", 0.0f, 1.0f, 0.6f });
    parameters.add({ "reverbDamping", "Reverb Damp", 0.0f, 1.0f, 0.5f });
    parameters.add({ "reverbWidth", "Reverb Width", 0.0f, 1.0f, 1.0f });
    parameters.add({ "limiterCeiling", "Ceiling", -12.0f, 0.0f, -0.3f }); // dBFS
    jassert(parameters.size() == numParameters);
}

void MasterBus::prepare(double newSampleRate, int) {
    sampleRate = newSampleRate;
    delayLine.setSize(2, static_cast<int>(maxDelaySeconds * sampleRate) + 1);
    reverb.setSampleRate(sampleRate);
    reset();
}

void MasterBus::reset() {
    delayLine.clear();
    delayWrite = 0;
    dampingState[0] = dampingState[1] = 0.0f;
    reverb.reset();
    limiterGain = 1.0f;
    primed = false;
}

void MasterBus::process(juce::AudioBuffer<float>& mix, juce::AudioBuffer<float>& delayBus,
                        juce::AudioBuffer<float>& reverbBus, int numSamples, double samplesPerBeat) {
    // Delay: wet-only return with damped feedback.
    const int length = delayLine.getNumSamples();
    const int delaySamples = juce::jlimit(1, length - 1, juce::roundToInt(parameters.get(delayBeats) * samplesPerBeat));
    const float feedback = parameters.get(delayFeedback);
    const float damping = parameters.get(delayDamping);

    for (int ch = 0; ch < 2; ch++) {
        float* line = delayLine.getWritePointer(ch);
        const float* in = delayBus.getReadPointer(ch);
        float* out = mix.getWritePointer(ch);
        int write = delayWrite;
        float state = dampingState[ch];

        for (int i = 0; i < numSamples; i++) {
            int read = write - delaySamples;
            if (read < 0) {
                read += length;
            }
            const float delayed = line[read];
            state = delayed + damping * (state - delayed); // one-pole lowpass in the feedback path
            line[write] = in[i] + feedback * state;
            out[i] += delayed;
            if (++write == length) {
                write = 0;
            }
        }
        dampingState[ch] = state;
    }
    delayWrite = (delayWrite + numSamples) % length;

    // Reverb: wet-only return.
    juce::Reverb::Parameters reverbParameters;
    reverbParameters.roomSize = parameters.get(reverbSize);
    reverbParameters.damping = parameters.get(reverbDamping);
    reverbParameters.width = parameters.get(reverbWidth);
    reverbParameters.wetLevel = 1.0f;
    reverbParameters.dryLevel = 0.0f;
    reverb.setParameters(reverbParameters);
    reverb.processStereo(reverbBus.getWritePointer(0), reverbBus.getWritePointer(1), numSamples);
    for (int ch = 0; ch < 2; ch++) {
        mix.addFrom(ch, 0, reverbBus, ch, 0, numSamples);
    }

    // Volume.
    const float volumeValue = parameters.get(volume);
    if (!primed) {
        lastVolume = volumeValue;
        primed = true;
    }
    mix.applyGainRamp(0, numSamples, lastVolume, volumeValue);
    lastVolume = volumeValue;

    // Peak limiter: instant attack (so the ceiling always holds), ~80 ms release.
    const float ceiling = juce::Decibels::decibelsToGain(parameters.get(limiterCeiling));
    const auto release = static_cast<float>(std::exp(-1.0 / (0.08 * sampleRate)));
    float* left = mix.getWritePointer(0);
    float* right = mix.getWritePointer(1);
    for (int i = 0; i < numSamples; i++) {
        const float peak = juce::jmax(std::abs(left[i]), std::abs(right[i]));
        limiterGain = 1.0f + (limiterGain - 1.0f) * release;
        if (peak * limiterGain > ceiling) {
            limiterGain = ceiling / peak;
        }
        left[i] *= limiterGain;
        right[i] *= limiterGain;
    }
}
