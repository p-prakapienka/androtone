#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

// Metronome click: a short decaying sine burst, higher and louder on the downbeat. Rendered
// straight into the main output (it bypasses the track mixer, like a preview / cue signal).
class Metronome {
public:
    void prepare(double newSampleRate) {
        sampleRate = newSampleRate;
        remaining = 0;
    }

    void setVolume(float newVolume) { volume = newVolume; }
    float getVolume() const { return volume; }

    void trigger(bool accent) {
        frequency = accent ? 1760.0 : 1320.0;
        amplitude = accent ? 1.0 : 0.6;
        phase = 0.0;
        total = remaining = static_cast<int>(sampleRate * 0.035);
    }

    // Adds the click to `buffer` in [start, start + numSamples).
    void render(juce::AudioBuffer<float>& buffer, int start, int numSamples) {
        const double delta = juce::MathConstants<double>::twoPi * frequency / sampleRate;
        for (int i = 0; i < numSamples && remaining > 0; i++, remaining--) {
            const double envelope = static_cast<double>(remaining) / total;
            const auto sample = static_cast<float>(std::sin(phase) * envelope * envelope * amplitude * volume);
            phase += delta;
            for (int ch = 0; ch < buffer.getNumChannels(); ch++) {
                buffer.addSample(ch, start + i, sample);
            }
        }
    }

private:
    double sampleRate = 44100.0;
    double frequency = 1320.0;
    double amplitude = 1.0;
    double phase = 0.0;
    float volume = 0.5f;
    int remaining = 0;
    int total = 1;
};
