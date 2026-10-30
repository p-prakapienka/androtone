#pragma once

#include "BytebeatProgram.h"
#include "VoiceMachine.h"
#include "../SwappableResource.h"

// Formula synth (the 8BitSynth slot in the spec): each voice runs a bytebeat formula, with the
// time counter `t` advancing at `rate` samples per second at middle C and transposed by the note.
// The formula is the machine's data (Machine::getData), compiled when set. 4 voices.
class Bytebeat : public VoiceMachine {
public:
    static constexpr const char* typeName = "Bytebeat";
    static constexpr const char* defaultFormula = "t*(42&t>>10)";

    enum ParameterIndex { rate, attack, release, level, numParameters };

    Bytebeat();
    juce::String getTypeName() const override { return typeName; }
    void process(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) override;

    // <Bytebeat formula="..."/>
    juce::ValueTree getData() const override;
    juce::Result setData(const juce::ValueTree& data) override;
    void collectGarbage() override { program.collect(); }

    static juce::ValueTree dataFor(const juce::String& formula);

private:
    SwappableResource<BytebeatProgram> program;
    const BytebeatProgram* blockProgram = nullptr;
};
