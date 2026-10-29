#pragma once

#include "Arrangement.h"
#include "Pattern.h"

#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>
#include <map>
#include <vector>

// The document: everything a song file stores. Lives on the message thread; the engine holds its
// own copy, kept in sync by Session.
using ParameterValues = std::map<juce::String, float>; // parameter id → value

struct EffectState {
    juce::String type; // empty = no effect in this slot
    ParameterValues parameters;
};

struct MachineState {
    static constexpr int numEffects = 2;

    juce::String type;
    juce::String name;
    ParameterValues parameters; // the machine's
    ParameterValues mixer;      // its channel strip's (see ChannelStrip)
    std::array<EffectState, numEffects> effects;
    BlockList blocks;           // its row on the song timeline
    std::array<Pattern, PatternBank::numPatterns> patterns {};
    int selectedPattern = 0;
};

struct Song {
    double tempo = 120.0;
    ParameterValues master; // the master bus's (see MasterBus)
    std::vector<MachineState> machines;

    // Song mode plays the timeline; pattern mode loops the selected patterns.
    bool songMode = false;
    bool loopEnabled = false;
    int loopStartBar = 0;
    int loopEndBar = 4;

    int endBar() const {
        int end = 0;
        for (const auto& machine : machines) {
            end = std::max(end, Arrangement::endBar(machine.blocks));
        }
        return end;
    }
};
