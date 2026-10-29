#include "TestToneMachine.h"

#include <cmath>

namespace {
    struct TestToneSound : juce::SynthesiserSound {
        bool appliesToNote(int) override { return true; }
        bool appliesToChannel(int) override { return true; }
    };

    class TestToneVoice : public juce::SynthesiserVoice {
    public:
        explicit TestToneVoice(const ParameterSet& parametersToUse) :
            parameters(parametersToUse) {}

        bool canPlaySound(juce::SynthesiserSound* sound) override {
            return dynamic_cast<TestToneSound*>(sound) != nullptr;
        }

        void startNote(int midiNote, float velocity, juce::SynthesiserSound*, int) override {
            phase = 0.0;
            phaseDelta = juce::MathConstants<double>::twoPi * juce::MidiMessage::getMidiNoteInHertz(midiNote)
                         / getSampleRate();
            // Parameters are sampled per note; smoothing within a note isn't needed for a test tone.
            level = parameters.get(TestToneMachine::level) * velocity;
            envelope.setSampleRate(getSampleRate());
            envelope.setParameters({ parameters.get(TestToneMachine::attack), 0.1f, 0.7f,
                                     parameters.get(TestToneMachine::release) });
            envelope.noteOn();
        }

        void stopNote(float, bool allowTailOff) override {
            if (allowTailOff) {
                envelope.noteOff();
            } else {
                envelope.reset();
                clearCurrentNote();
            }
        }

        void pitchWheelMoved(int) override {}
        void controllerMoved(int, int) override {}

        void renderNextBlock(juce::AudioBuffer<float>& output, int startSample, int numSamples) override {
            if (!isVoiceActive()) {
                return;
            }
            for (int i = startSample; i < startSample + numSamples; i++) {
                const float sample = static_cast<float>(std::sin(phase)) * level * envelope.getNextSample();
                phase += phaseDelta;
                if (phase >= juce::MathConstants<double>::twoPi) {
                    phase -= juce::MathConstants<double>::twoPi;
                }
                for (int ch = 0; ch < output.getNumChannels(); ch++) {
                    output.addSample(ch, i, sample);
                }
                if (!envelope.isActive()) {
                    clearCurrentNote();
                    return;
                }
            }
        }

    private:
        const ParameterSet& parameters;
        juce::ADSR envelope;
        double phase = 0.0;
        double phaseDelta = 0.0;
        float level = 0.0f;
    };
}

TestToneMachine::TestToneMachine() {
    parameters.add({ "level", "Level", 0.0f, 1.0f, 0.2f });
    parameters.add({ "attack", "Attack", 0.001f, 2.0f, 0.005f });
    parameters.add({ "release", "Release", 0.001f, 4.0f, 0.15f });
    jassert(parameters.size() == numParameters);

    for (int i = 0; i < 8; i++) {
        synth.addVoice(new TestToneVoice(parameters));
    }
    synth.addSound(new TestToneSound());
}

void TestToneMachine::prepare(double sampleRate, int) {
    synth.setCurrentPlaybackSampleRate(sampleRate);
}

void TestToneMachine::process(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) {
    synth.renderNextBlock(output, midi, 0, output.getNumSamples());
}

void TestToneMachine::reset() {
    synth.allNotesOff(0, false);
}
