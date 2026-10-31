#pragma once

#include "Pattern.h"

#include <algorithm>
#include <cmath>

// Note editing operations for the pattern editor, kept free of UI so they can be tested.
// Positions are in beats; callers quantise to their grid before calling.
namespace PatternEditing {
    constexpr double minimumLength = Pattern::stepLength / 4.0; // a 1/64 note
    constexpr float defaultVelocity = 0.8f;
    constexpr float accentVelocity = 1.0f;
    constexpr float softVelocity = 0.5f;

    inline double snap(double beat, double grid) {
        return grid > 0.0 ? std::floor(beat / grid + 1.0e-9) * grid : beat;
    }

    inline void sort(Pattern& pattern) {
        std::stable_sort(pattern.notes.begin(), pattern.notes.end(), [](const Note& a, const Note& b) {
            return !juce::exactlyEqual(a.start, b.start) ? a.start < b.start : a.pitch < b.pitch;
        });
    }

    // Index of the note at `pitch` sounding at `beat` (start <= beat < end), or -1.
    inline int noteAt(const Pattern& pattern, double beat, int pitch) {
        for (size_t i = 0; i < pattern.notes.size(); i++) {
            const auto& note = pattern.notes[i];
            if (note.pitch == pitch && beat >= note.start - 1.0e-9 && beat < note.start + note.length - 1.0e-9) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // Adds a note (replacing one that starts at the same place and pitch); returns its index, or -1
    // if it would start outside the pattern.
    inline int add(Pattern& pattern, double start, int pitch, double length = Pattern::stepLength,
                   float velocity = defaultVelocity) {
        if (start < 0.0 || start >= pattern.lengthInBeats() || pitch < 0 || pitch > 127) {
            return -1;
        }
        pattern.notes.erase(std::remove_if(pattern.notes.begin(), pattern.notes.end(),
                                           [&](const Note& n) { return n.pitch == pitch && std::abs(n.start - start) < 1.0e-9; }),
                            pattern.notes.end());
        pattern.notes.push_back({ start, std::max(minimumLength, length), pitch, velocity });
        sort(pattern);
        for (size_t i = 0; i < pattern.notes.size(); i++) {
            if (pattern.notes[i].pitch == pitch && std::abs(pattern.notes[i].start - start) < 1.0e-9) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    inline bool remove(Pattern& pattern, int index) {
        if (index < 0 || index >= static_cast<int>(pattern.notes.size())) {
            return false;
        }
        pattern.notes.erase(pattern.notes.begin() + index);
        return true;
    }

    // A tap on the grid: removes the note there, or adds one. Returns true if a note was added.
    inline bool toggle(Pattern& pattern, double beat, int pitch, double grid = Pattern::stepLength) {
        const int existing = noteAt(pattern, beat, pitch);
        if (existing >= 0) {
            remove(pattern, existing);
            return false;
        }
        return add(pattern, snap(beat, grid), pitch, grid) >= 0;
    }

    // Moves a note, keeping it inside the pattern and the MIDI range. Returns its new index.
    inline int move(Pattern& pattern, int index, double newStart, int newPitch) {
        if (index < 0 || index >= static_cast<int>(pattern.notes.size())) {
            return -1;
        }
        Note note = pattern.notes[static_cast<size_t>(index)];
        remove(pattern, index);
        const double latestStart = pattern.lengthInBeats() - minimumLength;
        return add(pattern, std::clamp(newStart, 0.0, latestStart), std::clamp(newPitch, 0, 127), note.length, note.velocity);
    }

    // Notes may run past the pattern end (they then ring into the next loop), up to one pattern length.
    inline bool resize(Pattern& pattern, int index, double newLength) {
        if (index < 0 || index >= static_cast<int>(pattern.notes.size())) {
            return false;
        }
        pattern.notes[static_cast<size_t>(index)].length = std::clamp(newLength, minimumLength, pattern.lengthInBeats());
        return true;
    }

    // Default → accent → soft → default. (Accent matters to Acid; see AcidBass::accentThreshold.)
    inline bool cycleVelocity(Pattern& pattern, int index) {
        if (index < 0 || index >= static_cast<int>(pattern.notes.size())) {
            return false;
        }
        auto& velocity = pattern.notes[static_cast<size_t>(index)].velocity;
        velocity = velocity >= accentVelocity - 1.0e-3f ? softVelocity
                   : velocity <= softVelocity + 1.0e-3f ? defaultVelocity
                                                        : accentVelocity;
        return true;
    }

    // Changes the length. Notes beyond the new end are kept (and play again if it grows back).
    inline bool setBars(Pattern& pattern, int bars) {
        if (!Pattern::isValidLength(bars)) {
            return false;
        }
        pattern.bars = bars;
        return true;
    }

    // Moves every note by `semitones`. Changes nothing (returns false) if a note would leave 0..127.
    inline bool transpose(Pattern& pattern, int semitones) {
        for (const auto& note : pattern.notes) {
            if (note.pitch + semitones < 0 || note.pitch + semitones > 127) {
                return false;
            }
        }
        for (auto& note : pattern.notes) {
            note.pitch += semitones;
        }
        sort(pattern);
        return true;
    }

    // Doubles the length and repeats the pattern (notes and automation) into the new half. Hidden
    // notes past the old end are dropped, since the copy now sits there. Fails at 8 bars.
    inline bool doubleLength(Pattern& pattern) {
        const int bars = pattern.bars * 2;
        if (!Pattern::isValidLength(bars)) {
            return false;
        }
        const double length = pattern.lengthInBeats();
        std::vector<Note> notes;
        for (const auto& note : pattern.notes) {
            if (note.start < length - 1.0e-9) {
                notes.push_back(note);
            }
        }
        const size_t count = notes.size();
        for (size_t i = 0; i < count; i++) {
            Note copy = notes[i];
            copy.start += length;
            notes.push_back(copy);
        }
        for (auto& lane : pattern.automation) {
            const auto original = lane.points;
            lane.points.clear();
            for (const auto& point : original) {
                if (point.beat < length - 1.0e-9) {
                    lane.setPoint(point.beat, point.value);
                    lane.setPoint(point.beat + length, point.value);
                } else if (point.beat < length + 1.0e-9) {
                    lane.setPoint(2.0 * length, point.value); // a point on the old end moves to the new end
                }
            }
        }
        pattern.bars = bars;
        pattern.notes = std::move(notes);
        sort(pattern);
        return true;
    }

    // Removes every note and automation lane; keeps the length.
    inline void clear(Pattern& pattern) {
        pattern.notes.clear();
        pattern.automation.clear();
    }

    // A copied pattern as pasted onto a machine: onto another machine type only the notes go, as its
    // automation lanes name the source machine's parameters.
    inline Pattern forPaste(Pattern pattern, bool sameMachineType) {
        if (!sameMachineType) {
            pattern.automation.clear();
        }
        return pattern;
    }
}
