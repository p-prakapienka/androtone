#include "BytebeatProgram.h"

#include <cctype>
#include <cstring>

// Recursive-descent compiler. Each level emits postfix code for its operands, then its operator.
class BytebeatCompiler {
public:
    explicit BytebeatCompiler(const juce::String& source) :
        text(source.toStdString()) {}

    juce::Result run(BytebeatProgram& program) {
        code.clear();
        depth = maxDepth = 0;
        if (!ternary()) {
            return failure();
        }
        skipSpace();
        if (position < text.size()) {
            error = "Unexpected '" + juce::String::charToString(static_cast<juce::juce_wchar>(text[position])) + "'";
            return failure();
        }
        if (maxDepth > BytebeatProgram::maxStack) {
            return juce::Result::fail("Formula is too deeply nested");
        }
        program.code = code;
        return juce::Result::ok();
    }

private:
    using Op = BytebeatProgram::Op;

    std::string text;
    size_t position = 0;
    std::vector<BytebeatProgram::Instruction> code;
    juce::String error;
    int depth = 0;
    int maxDepth = 0;

    juce::Result failure() const {
        return juce::Result::fail((error.isEmpty() ? juce::String("Invalid formula") : error) + " at position "
                                  + juce::String(static_cast<int>(position) + 1));
    }

    void skipSpace() {
        while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position]))) {
            position++;
        }
    }

    bool accept(const char* token) {
        skipSpace();
        const size_t length = std::strlen(token);
        if (text.compare(position, length, token) != 0) {
            return false;
        }
        // Don't take "<" out of "<<", "<=" etc.
        if (length == 1 && position + 1 < text.size()) {
            const char next = text[position + 1];
            if ((token[0] == '<' || token[0] == '>') && (next == token[0] || next == '=')) {
                return false;
            }
            if ((token[0] == '&' && next == '&') || (token[0] == '|' && next == '|')) {
                return false;
            }
            if (token[0] == '!' && next == '=') {
                return false;
            }
        }
        position += length;
        return true;
    }

    static int stackEffect(Op op) {
        switch (op) {
            case Op::push:
            case Op::time:
                return 1;
            case Op::neg:
            case Op::bitNot:
            case Op::logicalNot:
                return 0;
            case Op::select:
                return -2;
            case Op::add: case Op::sub: case Op::mul: case Op::div: case Op::mod: case Op::shl: case Op::shr:
            case Op::bitAnd: case Op::bitOr: case Op::bitXor: case Op::lt: case Op::le: case Op::gt: case Op::ge:
            case Op::eq: case Op::ne:
                return -1;
        }
        return 0;
    }

    void emit(Op op, uint32_t value = 0) {
        code.push_back({ op, value });
        depth += stackEffect(op);
        maxDepth = std::max(maxDepth, depth);
    }

    bool primary() {
        skipSpace();
        if (position >= text.size()) {
            error = "Unexpected end of formula";
            return false;
        }
        const char c = text[position];
        if (c == '(') {
            position++;
            if (!ternary()) {
                return false;
            }
            if (!accept(")")) {
                error = "Missing ')'";
                return false;
            }
            return true;
        }
        if (c == 't') {
            position++;
            emit(Op::time);
            return true;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            uint64_t value = 0;
            if (c == '0' && position + 1 < text.size() && (text[position + 1] == 'x' || text[position + 1] == 'X')) {
                position += 2;
                if (position >= text.size() || !std::isxdigit(static_cast<unsigned char>(text[position]))) {
                    error = "Bad hex number";
                    return false;
                }
                while (position < text.size() && std::isxdigit(static_cast<unsigned char>(text[position]))) {
                    value = (value * 16 + static_cast<uint64_t>(juce::CharacterFunctions::getHexDigitValue(static_cast<juce::juce_wchar>(text[position++])))) & 0xffffffffu;
                }
            } else {
                while (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position]))) {
                    value = (value * 10 + static_cast<uint64_t>(text[position++] - '0')) & 0xffffffffu;
                }
            }
            emit(Op::push, static_cast<uint32_t>(value));
            return true;
        }
        error = "Unexpected '" + juce::String::charToString(static_cast<juce::juce_wchar>(c)) + "'";
        return false;
    }

    bool unary() {
        if (accept("-")) {
            if (!unary()) return false;
            emit(Op::neg);
            return true;
        }
        if (accept("~")) {
            if (!unary()) return false;
            emit(Op::bitNot);
            return true;
        }
        if (accept("!")) {
            if (!unary()) return false;
            emit(Op::logicalNot);
            return true;
        }
        if (accept("+")) {
            return unary();
        }
        return primary();
    }

    template <typename Next>
    bool binary(Next next, std::initializer_list<std::pair<const char*, Op>> operators) {
        if (!(this->*next)()) {
            return false;
        }
        for (;;) {
            bool matched = false;
            for (const auto& [token, op] : operators) {
                if (accept(token)) {
                    if (!(this->*next)()) {
                        return false;
                    }
                    emit(op);
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                return true;
            }
        }
    }

    bool multiplicative() { return binary(&BytebeatCompiler::unary, { { "*", Op::mul }, { "/", Op::div }, { "%", Op::mod } }); }
    bool additive() { return binary(&BytebeatCompiler::multiplicative, { { "+", Op::add }, { "-", Op::sub } }); }
    bool shift() { return binary(&BytebeatCompiler::additive, { { "<<", Op::shl }, { ">>", Op::shr } }); }
    bool relational() {
        return binary(&BytebeatCompiler::shift, { { "<=", Op::le }, { ">=", Op::ge }, { "<", Op::lt }, { ">", Op::gt } });
    }
    bool equality() { return binary(&BytebeatCompiler::relational, { { "==", Op::eq }, { "!=", Op::ne } }); }
    bool bitAnd() { return binary(&BytebeatCompiler::equality, { { "&", Op::bitAnd } }); }
    bool bitXor() { return binary(&BytebeatCompiler::bitAnd, { { "^", Op::bitXor } }); }
    bool bitOr() { return binary(&BytebeatCompiler::bitXor, { { "|", Op::bitOr } }); }

    bool ternary() {
        if (!bitOr()) {
            return false;
        }
        if (accept("?")) {
            if (!ternary()) return false;
            if (!accept(":")) {
                error = "Missing ':'";
                return false;
            }
            if (!ternary()) return false;
            emit(Op::select);
        }
        return true;
    }
};

juce::Result BytebeatProgram::compile(const juce::String& formula, BytebeatProgram& program) {
    if (formula.trim().isEmpty()) {
        return juce::Result::fail("The formula is empty");
    }
    BytebeatProgram compiled;
    const auto result = BytebeatCompiler(formula).run(compiled);
    if (result.wasOk()) {
        compiled.formula = formula;
        program = std::move(compiled);
    }
    return result;
}

uint8_t BytebeatProgram::evaluate(uint32_t t) const {
    uint32_t stack[maxStack];
    int top = -1;
    for (const auto& instruction : code) {
        // Binary operators combine the top two values into the lower one.
        const auto pop = [&]() { return stack[top--]; };
        switch (instruction.op) {
            case Op::push: stack[++top] = instruction.value; break;
            case Op::time: stack[++top] = t; break;
            case Op::neg: stack[top] = 0u - stack[top]; break;
            case Op::bitNot: stack[top] = ~stack[top]; break;
            case Op::logicalNot: stack[top] = stack[top] == 0 ? 1u : 0u; break;
            case Op::select: {
                const uint32_t no = pop();
                const uint32_t yes = pop();
                stack[top] = stack[top] != 0 ? yes : no;
                break;
            }
            case Op::add: { const uint32_t b = pop(); stack[top] = stack[top] + b; break; }
            case Op::sub: { const uint32_t b = pop(); stack[top] = stack[top] - b; break; }
            case Op::mul: { const uint32_t b = pop(); stack[top] = stack[top] * b; break; }
            case Op::div: { const uint32_t b = pop(); stack[top] = b == 0 ? 0 : stack[top] / b; break; }
            case Op::mod: { const uint32_t b = pop(); stack[top] = b == 0 ? 0 : stack[top] % b; break; }
            case Op::shl: { const uint32_t b = pop(); stack[top] = b >= 32 ? 0 : stack[top] << b; break; }
            case Op::shr: { const uint32_t b = pop(); stack[top] = b >= 32 ? 0 : stack[top] >> b; break; }
            case Op::bitAnd: { const uint32_t b = pop(); stack[top] &= b; break; }
            case Op::bitOr: { const uint32_t b = pop(); stack[top] |= b; break; }
            case Op::bitXor: { const uint32_t b = pop(); stack[top] ^= b; break; }
            case Op::lt: { const uint32_t b = pop(); stack[top] = stack[top] < b; break; }
            case Op::le: { const uint32_t b = pop(); stack[top] = stack[top] <= b; break; }
            case Op::gt: { const uint32_t b = pop(); stack[top] = stack[top] > b; break; }
            case Op::ge: { const uint32_t b = pop(); stack[top] = stack[top] >= b; break; }
            case Op::eq: { const uint32_t b = pop(); stack[top] = stack[top] == b; break; }
            case Op::ne: { const uint32_t b = pop(); stack[top] = stack[top] != b; break; }
        }
    }
    return top >= 0 ? static_cast<uint8_t>(stack[top] & 0xffu) : 0;
}
