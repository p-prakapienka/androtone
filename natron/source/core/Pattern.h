#pragma once

#include "Automation.h"
#include "Transport.h"

#include <juce_core/juce_core.h>
#include <vector>

// A note in a pattern, in beats from the pattern start. Notes may overlap (chords) and may be
// longer than a step or run past the end of the pattern; the player handles both.
struct Note {
    double start = 0.0;
    double length = 0.25;
    int pitch = 60;
    float velocity = 0.8f; // 0..1

    bool operator==(const Note& other) const {
        return juce::exactlyEqual(start, other.start) && juce::exactlyEqual(length, other.length)
               && pitch == other.pitch && juce::exactlyEqual(velocity, other.velocity);
    }
};

// One machine pattern. Caustic patterns are 1, 2, 4 or 8 bars long.
struct Pattern {
    static constexpr double stepLength = 0.25; // a 1/16 step, the default editing grid

    int bars = 1;
    std::vector<Note> notes;
    AutomationLanes automation; // targets this pattern's machine; beats from the pattern start

    double lengthInBeats() const { return bars * Transport::beatsPerBar; }
    bool isEmpty() const { return notes.empty() && automation.empty(); }

    static bool isValidLength(int numBars) {
        return numBars == 1 || numBars == 2 || numBars == 4 || numBars == 8;
    }

    bool operator==(const Pattern& other) const {
        return bars == other.bars && notes == other.notes && automation == other.automation;
    }
};

// Every machine has 4 banks (A-D) of 16 patterns: A01 ... D16, indexed 0 ... 63.
struct PatternBank {
    static constexpr int numBanks = 4;
    static constexpr int patternsPerBank = 16;
    static constexpr int numPatterns = numBanks * patternsPerBank;

    static int indexOf(int bank, int slot) { return bank * patternsPerBank + slot; }
    static int bankOf(int index) { return index / patternsPerBank; }
    static int slotOf(int index) { return index % patternsPerBank; }

    static juce::String nameOf(int index) {
        return juce::String::charToString(static_cast<juce::juce_wchar>('A' + bankOf(index)))
               + juce::String(slotOf(index) + 1).paddedLeft('0', 2);
    }
};
