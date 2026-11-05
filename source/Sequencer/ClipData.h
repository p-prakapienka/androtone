#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

// One note inside a clip. Times are in beats (quarter notes) from the clip's zero point.
struct MidiNote {
    double start = 0.0;
    double length = 0.25;
    int pitch = 60;
    int velocity = 100;
    bool muted = false; // a deactivated note stays in the clip but doesn't play

    double end() const { return start + length; }

    bool operator==(const MidiNote& other) const {
        return start == other.start && length == other.length && pitch == other.pitch
            && velocity == other.velocity && muted == other.muted;
    }
};

// The content of a MIDI clip: its notes and its markers. Instances are immutable once handed to a
// Clip slot — editors copy, modify and swap in a new instance, so the audio thread can keep reading
// the old one without locks (see Clip::setData and ReleasePool).
//
// Playback starts at `startMarker`. A looping clip plays on to `loopEnd`, then repeats
// [loopStart, loopEnd) — so it can run into a loop that begins later. A non-looping clip plays
// [startMarker, endMarker) once and stops.
struct ClipData : juce::ReferenceCountedObject {
    using Ptr = juce::ReferenceCountedObjectPtr<ClipData>;

    static constexpr double DEFAULT_LENGTH = 4.0; // one 4/4 bar

    std::vector<MidiNote> notes; // kept sorted by start (see sortNotes)
    juce::String name;
    juce::uint32 colour = 0;     // ARGB; 0 = use the track colour

    bool looping = true;
    double startMarker = 0.0;
    double endMarker = DEFAULT_LENGTH;
    double loopStart = 0.0;
    double loopEnd = DEFAULT_LENGTH;

    ClipData() = default;
    ClipData(const ClipData& other) :
        juce::ReferenceCountedObject(),
        notes(other.notes), name(other.name), colour(other.colour), looping(other.looping),
        startMarker(other.startMarker), endMarker(other.endMarker), loopStart(other.loopStart),
        loopEnd(other.loopEnd) {}

    static Ptr createEmpty(double length = DEFAULT_LENGTH) {
        Ptr data = new ClipData();
        data->setLength(length);
        return data;
    }

    Ptr copy() const {
        return new ClipData(*this);
    }

    // Sets start/end and the loop to [0, length).
    void setLength(double length) {
        length = juce::jmax(MIN_LENGTH, length);
        startMarker = 0.0;
        endMarker = length;
        loopStart = 0.0;
        loopEnd = length;
    }

    // Where the clip's playback region ends (the loop end when looping).
    double getPlayEnd() const {
        return looping ? loopEnd : endMarker;
    }

    // Length of one pass: start marker to the end of the region (the first pass of a run-into-loop).
    double getFirstPassLength() const {
        return juce::jmax(MIN_LENGTH, getPlayEnd() - startMarker);
    }

    double getLoopLength() const {
        return juce::jmax(MIN_LENGTH, loopEnd - loopStart);
    }

    // Visible / editable extent of the clip.
    double getExtent() const {
        return juce::jmax(endMarker, loopEnd);
    }

    void sortNotes() {
        std::stable_sort(notes.begin(), notes.end(), [](const MidiNote& a, const MidiNote& b) {
            return a.start < b.start || (a.start == b.start && a.pitch < b.pitch);
        });
    }

    // Keeps markers ordered and non-degenerate after an edit.
    void sanitise() {
        startMarker = juce::jmax(0.0, startMarker);
        endMarker = juce::jmax(startMarker + MIN_LENGTH, endMarker);
        loopStart = juce::jmax(0.0, loopStart);
        loopEnd = juce::jmax(loopStart + MIN_LENGTH, loopEnd);
        if (looping && startMarker >= loopEnd) {
            startMarker = loopStart;
        }
        sortNotes();
    }

    static constexpr double MIN_LENGTH = 1.0 / 16.0;
};

// Note-editing operations shared by the Clip tab and the tests. Each takes the indices to act on.
namespace ClipEdits {
    // §8.9: the loop doubles; its notes are copied after it, and later notes shift along.
    inline void duplicateLoop(ClipData& d) {
        const double from = d.looping ? d.loopStart : d.startMarker;
        const double to = d.looping ? d.loopEnd : d.endMarker;
        const double length = to - from;
        std::vector<MidiNote> result;
        for (auto note : d.notes) {
            if (note.start >= to - 1e-9) {
                note.start += length;
                result.push_back(note);
            } else {
                result.push_back(note);
                if (note.start >= from - 1e-9) {
                    note.start += length;
                    result.push_back(note);
                }
            }
        }
        d.notes = result;
        if (d.looping) {
            d.loopEnd += length;
        }
        if (d.endMarker >= to - 1e-9) {
            d.endMarker += length;
        }
    }

    // §10.5.11: move note starts `amount` (0..1) of the way to the nearest grid line.
    inline void quantize(ClipData& d, const std::vector<int>& indices, double grid, double amount) {
        for (int index : indices) {
            auto& note = d.notes[static_cast<size_t>(index)];
            const double target = std::round(note.start / grid) * grid;
            note.start = std::max(0.0, note.start + (target - note.start) * amount);
        }
    }

    inline void transpose(ClipData& d, const std::vector<int>& indices, int semitones) {
        for (int index : indices) {
            auto& note = d.notes[static_cast<size_t>(index)];
            note.pitch = juce::jlimit(0, 127, note.pitch + semitones);
        }
    }

    // §10.5.4: a note placed over another note's start replaces it; over its end, shortens it.
    inline std::vector<MidiNote> resolveOverlaps(const std::vector<MidiNote>& notes, int editedIndex) {
        const MidiNote edited = notes[static_cast<size_t>(editedIndex)];
        std::vector<MidiNote> result;
        for (int i = 0; i < static_cast<int>(notes.size()); i++) {
            MidiNote other = notes[static_cast<size_t>(i)];
            if (i != editedIndex && other.pitch == edited.pitch) {
                if (other.start >= edited.start && other.start < edited.end()) {
                    continue;
                }
                if (other.start < edited.start && other.end() > edited.start) {
                    other.length = edited.start - other.start;
                }
            }
            result.push_back(other);
        }
        return result;
    }
}

// Keeps replaced ClipData alive until no other thread holds it; collect() (called periodically from
// the message thread by the processor) then frees it. The audio thread therefore never runs a
// destructor or frees memory.
class ReleasePool {
public:
    static ReleasePool& get() {
        static ReleasePool pool;
        return pool;
    }

    void retain(juce::ReferenceCountedObject* object) {
        if (object == nullptr) {
            return;
        }
        const std::lock_guard<std::mutex> guard(mutex);
        pool.emplace_back(object);
    }

    // Frees everything only the pool still references. Safe to call at any time from a
    // non-audio thread.
    void collect() {
        const std::lock_guard<std::mutex> guard(mutex);
        pool.erase(std::remove_if(pool.begin(), pool.end(),
                                  [](const auto& object) { return object->getReferenceCount() <= 1; }),
                   pool.end());
    }

    size_t size() {
        const std::lock_guard<std::mutex> guard(mutex);
        return pool.size();
    }

private:
    std::mutex mutex;
    std::vector<juce::ReferenceCountedObjectPtr<juce::ReferenceCountedObject>> pool;
};
