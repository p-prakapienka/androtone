#pragma once

#include "../Machine.h"

// Base for polyphonic machines built on juce::Synthesiser: voices read the machine's parameters
// directly (atomics), the machine handles preparing and rendering them.
class MachineVoice : public juce::SynthesiserVoice {
public:
    explicit MachineVoice(const ParameterSet& parametersToUse) :
        parameters(parametersToUse) {}

    // Message thread, before the voice is used and whenever the device changes.
    virtual void prepare(double sampleRate, int maxBlockSize) { juce::ignoreUnused(sampleRate, maxBlockSize); }

    bool canPlaySound(juce::SynthesiserSound*) override { return true; }
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}

protected:
    const ParameterSet& parameters;
    float get(int index) const { return parameters.get(index); }
};

class VoiceMachine : public Machine {
public:
    void prepare(double sampleRate, int maxBlockSize) override {
        synth.setCurrentPlaybackSampleRate(sampleRate);
        for (int i = 0; i < synth.getNumVoices(); i++) {
            static_cast<MachineVoice*>(synth.getVoice(i))->prepare(sampleRate, maxBlockSize);
        }
    }

    void process(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) override {
        synth.renderNextBlock(output, midi, 0, output.getNumSamples());
    }

    void reset() override { synth.allNotesOff(0, false); }

protected:
    template <typename VoiceType>
    void addVoices(int count) {
        for (int i = 0; i < count; i++) {
            synth.addVoice(new VoiceType(parameters, i));
        }
        synth.addSound(new AnySound());
    }

    juce::Synthesiser synth;

private:
    struct AnySound : juce::SynthesiserSound {
        bool appliesToNote(int) override { return true; }
        bool appliesToChannel(int) override { return true; }
    };
};
