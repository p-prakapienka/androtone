#pragma once

#include "VoiceMachine.h"

// Four-operator FM synth (the FMSynth slot in the spec). Each operator is a sine with a frequency
// ratio, output level and ADSR; the algorithm decides which operators modulate which, and which
// are heard (carriers). Operator 4 can feed back into itself. 8 voices.
class FmSynth : public VoiceMachine {
public:
    static constexpr const char* typeName = "FM";
    static constexpr int numOperators = 4;
    static constexpr int numAlgorithms = 8;
    enum OperatorParameter { ratio, opLevel, attack, decay, sustain, release, parametersPerOperator };
    enum GlobalParameter { algorithm = numOperators * parametersPerOperator, feedback, level, numParameters };

    static int operatorParameter(int op, OperatorParameter p) { return op * parametersPerOperator + p; }

    // modulators[algorithm][op] = bit mask of the operators that modulate `op`; carriers[algorithm]
    // = bit mask of the operators that are heard. Operators are numbered 0-3 (1-4 in the UI).
    static constexpr int modulators[numAlgorithms][numOperators] = {
        { 0b0010, 0b0100, 0b1000, 0 },      // 0: 4 > 3 > 2 > 1
        { 0b0010, 0b1100, 0, 0 },           // 1: (3 + 4) > 2 > 1
        { 0b0110, 0, 0b1000, 0 },           // 2: (2 + (4 > 3)) > 1
        { 0b1010, 0b0100, 0, 0 },           // 3: ((3 > 2) + 4) > 1
        { 0b0010, 0, 0b1000, 0 },           // 4: (2 > 1) + (4 > 3)
        { 0b1000, 0b1000, 0b1000, 0 },      // 5: 4 > (1, 2, 3)
        { 0, 0, 0b1000, 0 },                // 6: (4 > 3) + 2 + 1
        { 0, 0, 0, 0 },                     // 7: 1 + 2 + 3 + 4
    };
    static constexpr int carriers[numAlgorithms] = { 0b0001, 0b0001, 0b0001, 0b0001, 0b0101, 0b0111, 0b0111, 0b1111 };

    FmSynth();
    juce::String getTypeName() const override { return typeName; }
};
