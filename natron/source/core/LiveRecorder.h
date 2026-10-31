#pragma once

#include "Engine.h"
#include "PatternEditing.h"

#include <map>
#include <vector>

// Pairs the engine's recorded note-ons and note-offs into pattern notes (message thread). Starts
// and lengths are quantised to `grid` (0 = as played); a note held across the loop point keeps its
// full length (it rings into the next loop).
class LiveRecorder {
public:
    struct Recorded {
        int machine = 0;
        int pattern = 0;
        Note note;
    };

    double grid = Pattern::stepLength;

    // Feeds one event; returns true and fills `finished` when it completes a note.
    bool handle(const Engine::RecordedEvent& event, Recorded& finished) {
        if (event.velocity > 0.0f) {
            held[event.pitch] = event; // a repeated note-on restarts the note
            return false;
        }
        const auto it = held.find(event.pitch);
        if (it == held.end()) {
            return false; // released a key pressed before recording started
        }
        finished = make(it->second, event.beat);
        held.erase(it);
        return true;
    }

    // Ends every held note at transport `beat` (recording or playback stopped).
    std::vector<Recorded> finishAll(double beat) {
        std::vector<Recorded> notes;
        for (const auto& [pitch, on] : held) {
            notes.push_back(make(on, beat));
        }
        held.clear();
        return notes;
    }

    bool isHolding() const { return !held.empty(); }

private:
    std::map<int, Engine::RecordedEvent> held; // by pitch

    Recorded make(const Engine::RecordedEvent& on, double offBeat) const {
        double start = on.patternBeat;
        double length = juce::jmax(0.0, offBeat - on.beat);
        if (grid > 0.0) {
            start = std::round(start / grid) * grid;
            if (start >= on.patternLength - 1.0e-9) {
                start = 0.0; // rounded onto the loop point: the pattern's first step
            }
            length = juce::jmax(grid, std::round(length / grid) * grid);
        }
        length = juce::jlimit(PatternEditing::minimumLength, on.patternLength, length);
        return { on.machine, on.pattern, { start, length, on.pitch, juce::jlimit(0.0f, 1.0f, on.velocity) } };
    }
};
