#pragma once

#include "Pattern.h"

#include <juce_core/juce_core.h>
#include <array>
#include <map>
#include <vector>

// The document: everything a song file stores. Lives on the message thread; the engine holds its
// own copy, kept in sync by Session.
struct MachineState {
    juce::String type;
    juce::String name;
    float volume = 0.8f;
    std::map<juce::String, float> parameters; // parameter id → value
    std::array<Pattern, PatternBank::numPatterns> patterns {};
    int selectedPattern = 0;
};

struct Song {
    double tempo = 120.0;
    float masterVolume = 0.8f;
    std::vector<MachineState> machines;
};
