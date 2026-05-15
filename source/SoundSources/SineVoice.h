#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class SineVoice : public juce::SynthesiserVoice {
public:
    bool canPlaySound(juce::SynthesiserSound* s) override {
        return dynamic_cast<SineSound*>(s) != nullptr;
    }

    void startNote(int midiNote, float velocity, juce::SynthesiserSound*, int) override {
        currentAngle = 0.0;
        level = velocity * 0.15;
        tailOff = 0.0;

        const auto freq = juce::MidiMessage::getMidiNoteInHertz(midiNote);
        angleDelta = freq / getSampleRate() * juce::MathConstants<double>::twoPi;
    }

    void stopNote(float, bool allowTailOff) override {
        if (allowTailOff) {
            if (tailOff == 0.0)
                tailOff = 1.0;
        } else {
            clearCurrentNote();
            angleDelta = 0.0;
        }
    }

    void pitchWheelMoved(int) override         {}
    void controllerMoved(int, int) override    {}

    void renderNextBlock(juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override {
        if (angleDelta == 0.0)
            return;

        for (int i = 0; i < numSamples; ++i) {
            const auto sample = (float) (std::sin(currentAngle) * level * (tailOff > 0.0 ? tailOff : 1.0));

            for (int ch = buffer.getNumChannels(); --ch >= 0;)
                buffer.addSample(ch, startSample + i, sample);

            currentAngle += angleDelta;

            if (tailOff > 0.0) {
                tailOff *= 0.99;
                if (tailOff <= 0.005) {
                    clearCurrentNote();
                    angleDelta = 0.0;
                    break;
                }
            }
        }

        ++startSample;
    }

private:
    double currentAngle = 0.0;
    double angleDelta = 0.0;
    double level = 0.0;
    double tailOff = 0.0;
};
