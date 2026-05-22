#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

struct OnePoleLowpass {
    static constexpr double cutoffHz = 500.0;

    double state = 0.0;
    double alpha = 0.0;

    void prepare(double sampleRate) {
        const double dt = 1.0 / sampleRate;
        const double rc = 1.0 / (juce::MathConstants<double>::twoPi * cutoffHz);
        alpha = dt / (rc + dt);
        state = 0.0;
    }

    double process(double input) {
        state += alpha * (input - state);
        return state;
    }
};

class SawVoice : public juce::SynthesiserVoice {
public:
    bool canPlaySound(juce::SynthesiserSound* s) override {
        return dynamic_cast<SawSound*>(s) != nullptr;
    }

    void startNote(int midiNote, float velocity, juce::SynthesiserSound*, int) override {
        phase = 0.0;
        level = velocity * 0.15;
        tailOff = 0.0;

        const auto freq = juce::MidiMessage::getMidiNoteInHertz(midiNote);
        phaseDelta = freq / getSampleRate();

        filter.prepare(getSampleRate());
    }

    void stopNote(float, bool allowTailOff) override {
        if (allowTailOff) {
            if (tailOff == 0.0) {
                tailOff = 1.0;
            }
        } else {
            clearCurrentNote();
            phaseDelta = 0.0;
        }
    }

    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}

    void renderNextBlock(juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override {
        if (phaseDelta == 0.0) {
            return;
        }

        for (int i = 0; i < numSamples; ++i) {
            auto sample = (2.0 * phase - 1.0) * level * (tailOff > 0.0 ? tailOff : 1.0);
            sample = filter.process(sample);

            for (int ch = buffer.getNumChannels(); --ch >= 0;) {
                buffer.addSample(ch, startSample + i, static_cast<float>(sample));
            }

            phase += phaseDelta;
            if (phase >= 1.0) {
                phase -= 1.0;
            }

            if (tailOff > 0.0) {
                tailOff *= 0.99;
                if (tailOff <= 0.005) {
                    clearCurrentNote();
                    phaseDelta = 0.0;
                    break;
                }
            }
        }
    }

private:
    OnePoleLowpass filter;
    double phase = 0.0;
    double phaseDelta = 0.0;
    double level = 0.0;
    double tailOff = 0.0;
};
