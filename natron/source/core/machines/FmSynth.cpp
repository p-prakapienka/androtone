#include "FmSynth.h"

#include <array>
#include <cmath>

namespace {
    constexpr double twoPi = juce::MathConstants<double>::twoPi;
    constexpr double modulationScale = 4.0; // radians of phase deviation at full modulator level

    class Voice : public MachineVoice {
    public:
        Voice(const ParameterSet& parametersToUse, int) :
            MachineVoice(parametersToUse) {}

        void prepare(double sampleRate, int) override {
            for (auto& envelope : envelopes) {
                envelope.setSampleRate(sampleRate);
            }
        }

        void startNote(int midiNote, float velocity, juce::SynthesiserSound*, int) override {
            frequency = juce::MidiMessage::getMidiNoteInHertz(midiNote);
            gain = velocity;
            phases = {};
            feedbackState = {};
            for (int op = 0; op < FmSynth::numOperators; op++) {
                envelopes[static_cast<size_t>(op)].setParameters({ op_(op, FmSynth::attack), op_(op, FmSynth::decay),
                                                                   op_(op, FmSynth::sustain), op_(op, FmSynth::release) });
                envelopes[static_cast<size_t>(op)].noteOn();
            }
        }

        void stopNote(float, bool allowTailOff) override {
            if (!allowTailOff) {
                for (auto& envelope : envelopes) {
                    envelope.reset();
                }
                clearCurrentNote();
                return;
            }
            for (auto& envelope : envelopes) {
                envelope.noteOff();
            }
        }

        void renderNextBlock(juce::AudioBuffer<float>& output, int startSample, int numSamples) override {
            if (!isVoiceActive()) {
                return;
            }
            const int algorithm = juce::jlimit(0, FmSynth::numAlgorithms - 1, juce::roundToInt(get(FmSynth::algorithm)));
            const int carrierMask = FmSynth::carriers[algorithm];
            int carrierCount = 0;
            std::array<double, FmSynth::numOperators> increments {};
            std::array<float, FmSynth::numOperators> levels {};
            for (int op = 0; op < FmSynth::numOperators; op++) {
                increments[static_cast<size_t>(op)] = frequency * op_(op, FmSynth::ratio) / getSampleRate();
                levels[static_cast<size_t>(op)] = op_(op, FmSynth::opLevel);
                carrierCount += (carrierMask >> op) & 1;
            }
            const double feedbackAmount = get(FmSynth::feedback) * 1.5;
            const float outputLevel = get(FmSynth::level) * gain / static_cast<float>(juce::jmax(1, carrierCount));

            for (int i = startSample; i < startSample + numSamples; i++) {
                std::array<double, FmSynth::numOperators> outputs {};
                // Higher operators only ever modulate lower ones, so compute 4 down to 1.
                for (int op = FmSynth::numOperators - 1; op >= 0; op--) {
                    double modulation = 0.0;
                    const int mask = FmSynth::modulators[algorithm][op];
                    for (int source = op + 1; source < FmSynth::numOperators; source++) {
                        if ((mask >> source) & 1) {
                            modulation += outputs[static_cast<size_t>(source)] * modulationScale;
                        }
                    }
                    if (op == FmSynth::numOperators - 1) {
                        modulation += feedbackAmount * 0.5 * (feedbackState[0] + feedbackState[1]);
                    }
                    const double value = std::sin(twoPi * phases[static_cast<size_t>(op)] + modulation)
                                         * levels[static_cast<size_t>(op)] * envelopes[static_cast<size_t>(op)].getNextSample();
                    outputs[static_cast<size_t>(op)] = value;
                    phases[static_cast<size_t>(op)] += increments[static_cast<size_t>(op)];
                    phases[static_cast<size_t>(op)] -= std::floor(phases[static_cast<size_t>(op)]);
                }
                feedbackState[1] = feedbackState[0];
                feedbackState[0] = outputs[FmSynth::numOperators - 1];

                double sum = 0.0;
                for (int op = 0; op < FmSynth::numOperators; op++) {
                    if ((carrierMask >> op) & 1) {
                        sum += outputs[static_cast<size_t>(op)];
                    }
                }
                const auto sample = static_cast<float>(sum) * outputLevel;
                for (int ch = 0; ch < output.getNumChannels(); ch++) {
                    output.addSample(ch, i, sample);
                }
            }

            bool anyCarrierActive = false;
            for (int op = 0; op < FmSynth::numOperators; op++) {
                anyCarrierActive = anyCarrierActive || (((carrierMask >> op) & 1) && envelopes[static_cast<size_t>(op)].isActive());
            }
            if (!anyCarrierActive) {
                clearCurrentNote();
            }
        }

    private:
        double frequency = 440.0;
        float gain = 1.0f;
        std::array<double, FmSynth::numOperators> phases {};
        std::array<double, 2> feedbackState {};
        std::array<juce::ADSR, FmSynth::numOperators> envelopes;

        float op_(int op, FmSynth::OperatorParameter p) const { return get(FmSynth::operatorParameter(op, p)); }
    };
}

FmSynth::FmSynth() {
    // A classic electric-piano-ish default: algorithm 4, two modulator/carrier pairs.
    const float ratios[numOperators] = { 1.0f, 1.0f, 1.0f, 14.0f };
    const float levels[numOperators] = { 1.0f, 0.35f, 0.8f, 0.15f };
    const float decays[numOperators] = { 1.5f, 0.6f, 1.2f, 0.2f };
    for (int op = 0; op < numOperators; op++) {
        const juce::String n = juce::String(op + 1);
        parameters.add({ "op" + n + "Ratio", "Op" + n + " Ratio", 0.5f, 16.0f, ratios[op] });
        parameters.add({ "op" + n + "Level", "Op" + n + " Level", 0.0f, 1.0f, levels[op] });
        parameters.add({ "op" + n + "Attack", "Op" + n + " Attack", 0.001f, 4.0f, 0.002f });
        parameters.add({ "op" + n + "Decay", "Op" + n + " Decay", 0.001f, 4.0f, decays[op] });
        parameters.add({ "op" + n + "Sustain", "Op" + n + " Sustain", 0.0f, 1.0f, 0.2f });
        parameters.add({ "op" + n + "Release", "Op" + n + " Release", 0.001f, 4.0f, 0.3f });
    }
    parameters.add({ "algorithm", "Algorithm", 0.0f, static_cast<float>(numAlgorithms - 1), 4.0f, 1.0f });
    parameters.add({ "feedback", "Feedback", 0.0f, 1.0f, 0.0f });
    parameters.add({ "level", "Level", 0.0f, 1.0f, 0.4f });
    jassert(parameters.size() == numParameters);
    addVoices<Voice>(8);
}
