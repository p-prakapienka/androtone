#pragma once

#include "VoiceMachine.h"

// Karplus-Strong plucked string (the KSSynth slot in the spec): a noise burst circulating in a
// tuned, damped delay loop. 8 voices.
class Pluck : public VoiceMachine {
public:
    static constexpr const char* typeName = "Pluck";
    enum ParameterIndex { decay, damping, brightness, release, level, numParameters };

    Pluck();
    juce::String getTypeName() const override { return typeName; }
};
