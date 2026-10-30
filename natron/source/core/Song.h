#pragma once

#include "Arrangement.h"
#include "Pattern.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
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
    juce::ValueTree data;       // machine-specific data (see Machine::getData); treat as immutable
    std::array<Pattern, PatternBank::numPatterns> patterns {};
    int selectedPattern = 0;
};

struct Song {
    double tempo = 120.0;
    ParameterValues master; // the master bus's (see MasterBus)
    std::vector<MachineState> machines;

    // Song automation (song mode only): lanes on the song timeline, targeting any machine or the master.
    AutomationLanes automation;

    // Swing: where the off-beat 1/16 notes fall within each 1/8, from 0.5 (straight) to 0.75.
    static constexpr float minSwing = 0.5f;
    static constexpr float maxSwing = 0.75f;
    float swing = minSwing;

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
