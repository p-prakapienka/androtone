#pragma once

#include "Pattern.h"

#include <juce_core/juce_core.h>
#include <array>
#include <map>
#include <vector>

// The document: everything a song file stores. Lives on the message thread; the engine holds its
// own copy, kept in sync by Session.
using ParameterValues = std::map<juce::String, float>; // parameter id → value

struct MachineState {
    juce::String type;
    juce::String name;
    ParameterValues parameters; // the machine's
    ParameterValues mixer;      // its channel strip's (see ChannelStrip)
    std::array<Pattern, PatternBank::numPatterns> patterns {};
    int selectedPattern = 0;
};

struct Song {
    double tempo = 120.0;
    ParameterValues master; // the master bus's (see MasterBus)
    std::vector<MachineState> machines;
};
