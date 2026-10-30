#pragma once

#include <juce_core/juce_core.h>
#include <cstdint>
#include <vector>

// A compiled bytebeat formula: C-style integer arithmetic on the time counter `t`, e.g.
// "t*(42&t>>10)". Compiled on the message thread to a postfix program; evaluated on the audio
// thread with a fixed-size stack (no allocation). Output is the low 8 bits of the result.
//
// Supported: decimal and 0x hex numbers, t, parentheses, unary - ~ !, and with C precedence:
//   * / %   + -   << >>   < <= > >=   == !=   &   ^   |   ?:
// Division or modulo by zero gives 0.
class BytebeatProgram {
public:
    static constexpr int maxStack = 64;

    // Fails with a message and position for invalid formulas.
    static juce::Result compile(const juce::String& formula, BytebeatProgram& program);

    uint8_t evaluate(uint32_t t) const;

    const juce::String& getFormula() const { return formula; }

private:
    enum class Op : uint8_t {
        push, time, add, sub, mul, div, mod, shl, shr, bitAnd, bitOr, bitXor,
        lt, le, gt, ge, eq, ne, neg, bitNot, logicalNot, select,
    };

    struct Instruction {
        Op op;
        uint32_t value = 0;
    };

    std::vector<Instruction> code;
    juce::String formula;

    friend class BytebeatCompiler;
};
