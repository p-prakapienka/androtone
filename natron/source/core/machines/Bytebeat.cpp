#include "Bytebeat.h"

namespace {
    using P = Bytebeat::ParameterIndex;
    const juce::Identifier dataType { "Bytebeat" };
    const juce::Identifier formulaId { "formula" };

    class Voice : public MachineVoice {
    public:
        Voice(const ParameterSet& parametersToUse, int) :
            MachineVoice(parametersToUse) {}

        void setProgram(const BytebeatProgram* const* source) { program = source; }

        void prepare(double sampleRate, int) override { amp.setSampleRate(sampleRate); }

        void startNote(int midiNote, float velocity, juce::SynthesiserSound*, int) override {
            transpose = std::pow(2.0, (midiNote - 60) / 12.0);
            gain = velocity;
            time = 0.0;
            amp.setParameters({ get(P::attack), 0.0f, 1.0f, get(P::release) });
            amp.noteOn();
        }

        void stopNote(float, bool allowTailOff) override {
            if (allowTailOff) {
                amp.noteOff();
            } else {
                amp.reset();
                clearCurrentNote();
            }
        }

        void renderNextBlock(juce::AudioBuffer<float>& output, int startSample, int numSamples) override {
            const auto* code = program != nullptr ? *program : nullptr;
            if (!isVoiceActive() || code == nullptr) {
                return;
            }
            const double increment = get(P::rate) * transpose / getSampleRate();
            const float level = get(P::level) * gain;
            for (int i = startSample; i < startSample + numSamples; i++) {
                const auto byte = code->evaluate(static_cast<uint32_t>(static_cast<uint64_t>(time)));
                time += increment;
                const float sample = (static_cast<float>(byte) / 128.0f - 1.0f) * amp.getNextSample() * level;
                for (int ch = 0; ch < output.getNumChannels(); ch++) {
                    output.addSample(ch, i, sample);
                }
                if (!amp.isActive()) {
                    clearCurrentNote();
                    return;
                }
            }
        }

    private:
        const BytebeatProgram* const* program = nullptr;
        double transpose = 1.0;
        double time = 0.0;
        float gain = 1.0f;
        juce::ADSR amp;
    };
}

Bytebeat::Bytebeat() {
    parameters.add({ "rate", "Rate", 1000.0f, 44100.0f, 8000.0f }); // t steps per second at middle C
    parameters.add({ "attack", "Attack", 0.001f, 2.0f, 0.002f });
    parameters.add({ "release", "Release", 0.001f, 4.0f, 0.05f });
    parameters.add({ "level", "Level", 0.0f, 1.0f, 0.25f });
    jassert(parameters.size() == numParameters);

    addVoices<Voice>(4);
    for (int i = 0; i < synth.getNumVoices(); i++) {
        static_cast<Voice*>(synth.getVoice(i))->setProgram(&blockProgram);
    }
    setData(dataFor(defaultFormula));
}

void Bytebeat::process(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) {
    blockProgram = program.acquire();
    VoiceMachine::process(output, midi);
}

juce::ValueTree Bytebeat::dataFor(const juce::String& formula) {
    juce::ValueTree tree(dataType);
    tree.setProperty(formulaId, formula, nullptr);
    return tree;
}

juce::ValueTree Bytebeat::getData() const {
    const auto* current = program.get();
    return dataFor(current != nullptr ? current->getFormula() : juce::String(defaultFormula));
}

juce::Result Bytebeat::setData(const juce::ValueTree& data) {
    const auto formula = data.hasType(dataType) ? data.getProperty(formulaId).toString() : juce::String(defaultFormula);
    auto compiled = std::make_unique<BytebeatProgram>();
    const auto result = BytebeatProgram::compile(formula, *compiled);
    if (result.wasOk()) {
        program.set(std::move(compiled));
    }
    return result;
}
