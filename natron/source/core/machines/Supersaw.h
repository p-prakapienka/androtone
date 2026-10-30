#pragma once

#include "VoiceMachine.h"

// Detuned stacked saws spread across the stereo field (the SawSynth slot in the spec), through a
// low-pass filter with its own envelope. 8 voices.
class Supersaw : public VoiceMachine {
public:
    static constexpr const char* typeName = "Supersaw";
    static constexpr int maxSaws = 9;

    enum ParameterIndex {
        saws, detune, sideMix, cutoff, resonance, filterEnvAmount,
        filterAttack, filterDecay, filterSustain, filterRelease,
        ampAttack, ampDecay, ampSustain, ampRelease, level,
        numParameters
    };

    Supersaw();
    juce::String getTypeName() const override { return typeName; }
};
