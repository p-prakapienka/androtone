#pragma once

#include "Clip.h"
#include "ClipData.h"
#include "../Constants.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// A clip placed on the arrangement timeline: `content` played from its start marker (plus
// `offset` beats into its playback) for `length` beats starting at song beat `start`. Looping
// content repeats to fill the length; notes are cut at the clip's end.
struct ArrangementClip {
    double start = 0.0;
    double length = Clip::BEATS_PER_BAR;
    ClipData::Ptr content;
    double offset = 0.0;
    int sourceSlot = -1; // session slot it came from (for display), -1 if unknown

    double end() const { return start + length; }
};

// Immutable snapshot of the whole arrangement, swapped in like ClipData. `rendered` holds every
// track's notes flattened to song time, sorted by start, so playback is a simple window lookup.
struct ArrangementData : juce::ReferenceCountedObject {
    using Ptr = juce::ReferenceCountedObjectPtr<ArrangementData>;

    std::array<std::vector<ArrangementClip>, NUM_TRACKS> clips;
    std::array<std::vector<MidiNote>, NUM_TRACKS> rendered;

    ArrangementData() = default;
    ArrangementData(const ArrangementData& other) :
        juce::ReferenceCountedObject(), clips(other.clips), rendered(other.rendered) {}

    Ptr copy() const { return new ArrangementData(*this); }

    bool hasContent(int track) const {
        return !clips[static_cast<size_t>(track)].empty();
    }

    double getEnd() const {
        double end = 0.0;
        for (const auto& lane : clips) {
            for (const auto& clip : lane) {
                end = std::max(end, clip.end());
            }
        }
        return end;
    }

    // Places `clip` on `track`, cutting away whatever it covers (clips fully covered are removed;
    // partly covered ones are trimmed at the overlapping side).
    void place(int track, ArrangementClip clip) {
        auto& lane = clips[static_cast<size_t>(track)];
        std::vector<ArrangementClip> result;
        for (auto other : lane) {
            if (other.end() <= clip.start + 1e-9 || other.start >= clip.end() - 1e-9) {
                result.push_back(other);
                continue;
            }
            if (other.start < clip.start) {
                // Keep the part before the new clip...
                ArrangementClip head = other;
                head.length = clip.start - other.start;
                result.push_back(head);
            }
            if (other.end() > clip.end()) {
                // ...and the part after it.
                ArrangementClip tail = other;
                const double cut = clip.end() - other.start;
                tail.start = clip.end();
                tail.length = other.end() - clip.end();
                tail.offset = other.offset + cut;
                result.push_back(tail);
            }
        }
        result.push_back(clip);
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
        lane = result;
    }

    void remove(int track, int index) {
        auto& lane = clips[static_cast<size_t>(track)];
        if (index >= 0 && index < static_cast<int>(lane.size())) {
            lane.erase(lane.begin() + index);
        }
    }

    // Rebuilds the flattened note lists. Call after any change, before handing the snapshot over.
    void render() {
        for (int t = 0; t < NUM_TRACKS; t++) {
            auto& out = rendered[static_cast<size_t>(t)];
            out.clear();
            for (const auto& clip : clips[static_cast<size_t>(t)]) {
                renderClip(clip, out);
            }
            std::stable_sort(out.begin(), out.end(), [](const MidiNote& a, const MidiNote& b) { return a.start < b.start; });
        }
    }

    static void renderClip(const ArrangementClip& clip, std::vector<MidiNote>& out) {
        if (clip.content == nullptr || clip.length <= 0.0) {
            return;
        }
        const ClipData& data = *clip.content;
        const double playEnd = data.getPlayEnd();

        // Position in the content at the clip's start (offset may already be in the loop).
        double position = data.startMarker + clip.offset;
        if (position >= playEnd) {
            if (!data.looping) {
                return;
            }
            position = data.loopStart + std::fmod(position - playEnd, data.getLoopLength());
        }

        double elapsed = 0.0;
        for (int guard = 0; elapsed < clip.length - 1e-9 && guard < 100000; guard++) {
            const double segment = std::min(clip.length - elapsed, playEnd - position);
            for (const auto& note : data.notes) {
                if (note.muted || note.start < position - 1e-9 || note.start >= position + segment - 1e-9) {
                    continue;
                }
                MidiNote placed = note;
                placed.start = clip.start + elapsed + (note.start - position);
                placed.length = std::min(note.length, clip.end() - placed.start);
                if (placed.length > 1e-9) {
                    out.push_back(placed);
                }
            }
            elapsed += segment;
            position += segment;
            if (position >= playEnd - 1e-9) {
                if (!data.looping) {
                    break;
                }
                position = data.loopStart;
            }
        }
    }
};
