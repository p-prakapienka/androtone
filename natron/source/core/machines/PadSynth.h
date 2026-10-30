#pragma once

#include "VoiceMachine.h"
#include "../SwappableResource.h"

#include <vector>

// PADsynth-algorithm pad (the PadSynth slot in the spec): a long, seamlessly looping wavetable whose
// harmonics are each spread over a bandwidth with random phases, which gives an ensemble-like,
// evolving tone. The table is rebuilt on the message thread when a harmonic or bandwidth parameter
// changes (parameterChanged), then swapped in. 8 voices.
class PadSynth : public VoiceMachine {
public:
    static constexpr const char* typeName = "PadSynth";
    static constexpr int numHarmonics = 8;
    static constexpr int tableOrder = 16; // 65536 samples
    static constexpr double tableSampleRate = 48000.0;
    static constexpr double tableFrequency = tableSampleRate / (1 << tableOrder) * 300.0; // ~219.7 Hz, on a bin

    enum ParameterIndex {
        harmonic1 = 0, // ...harmonic8 = 7
        bandwidth = numHarmonics, bandwidthScale, attack, decay, sustain, release, level,
        numParameters
    };

    struct Table {
        std::vector<float> samples;
    };

    PadSynth();
    juce::String getTypeName() const override { return typeName; }
    void process(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) override;
    void parameterChanged(int index) override;
    void collectGarbage() override { table.collect(); }

    // Builds the table for the given harmonic levels and bandwidth (in cents, scaled per harmonic).
    static std::unique_ptr<Table> buildTable(const float* harmonicLevels, float bandwidthCents, float scale);

    const Table* currentTable() const { return table.get(); }

private:
    SwappableResource<Table> table;
    const Table* blockTable = nullptr; // what the voices use during this block
    void rebuild();
};
