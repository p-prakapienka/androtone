#pragma once

#include "../Machine.h"

// Placeholder machine: an 8-voice sine synth with a short attack/release envelope. It exists so
// the engine and tests have something audible before the real machines (SubSynth etc.) land.
class TestToneMachine : public Machine {
public:
    TestToneMachine();

    juce::String getTypeName() const override { return "TestTone"; }
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) override;
    void reset() override;

private:
    juce::Synthesiser synth;
};
