#pragma once

#include "VoiceMachine.h"

// Three-operator FM synth (the FMSynth slot in the spec). Each operator is a sine with a frequency
// ratio, output level and ADSR; the algorithm decides which operators modulate which, and which
// are heard (carriers). Operator 3 can feed back into itself. 8 voices.
class FmSynth : public VoiceMachine {
public:
    static constexpr const char* typeName = "FM";
    static constexpr int numOperators = 3;
    static constexpr int numAlgorithms = 6;
    enum OperatorParameter { ratio, opLevel, attack, decay, sustain, release, parametersPerOperator };
    enum GlobalParameter { algorithm = numOperators * parametersPerOperator, feedback, level, numParameters };

    static int operatorParameter(int op, OperatorParameter p) { return op * parametersPerOperator + p; }

    // modulators[algorithm][op] = bit mask of the operators that modulate `op`; carriers[algorithm]
    // = bit mask of the operators that are heard. Operators are numbered 0-2 (1-3 in the UI).
    // (Caustic's FMSynth has three operators; its exact algorithm list is still to verify.)
    static constexpr int modulators[numAlgorithms][numOperators] = {
        { 0b010, 0b100, 0 }, // 0: 3 > 2 > 1
        { 0b110, 0, 0 },     // 1: (2 + 3) > 1
        { 0b100, 0b100, 0 }, // 2: 3 > (1, 2)
        { 0, 0b100, 0 },     // 3: (3 > 2) + 1
        { 0b010, 0, 0 },     // 4: (2 > 1) + 3
        { 0, 0, 0 },         // 5: 1 + 2 + 3
    };
    static constexpr int carriers[numAlgorithms] = { 0b001, 0b001, 0b011, 0b011, 0b101, 0b111 };

    FmSynth();
    juce::String getTypeName() const override { return typeName; }
};
