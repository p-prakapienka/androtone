#pragma once

#include "Transport.h"

#include <vector>

// A note in a pattern, in beats from the pattern start. Notes may overlap (chords) and may be
// longer than a step or run past the end of the pattern; the player handles both.
struct Note {
    double start = 0.0;
    double length = 0.25;
    int pitch = 60;
    float velocity = 0.8f; // 0..1
};

// One machine pattern. Caustic patterns are 1, 2, 4 or 8 bars long.
struct Pattern {
    static constexpr double stepLength = 0.25; // a 1/16 step, the default editing grid

    int bars = 1;
    std::vector<Note> notes;

    double lengthInBeats() const { return bars * Transport::beatsPerBar; }
};
