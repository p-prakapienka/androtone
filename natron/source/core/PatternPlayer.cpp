#include "PatternPlayer.h"

#include <algorithm>
#include <cmath>

namespace {
    // Shortest note the player will produce; a zero-length note still gets a note-off one sample
    // after its note-on.
    constexpr double minimumNoteLength = 1.0e-6;
}

PatternPlayer::PatternPlayer() {
    sounding.reserve(128);
    noteOns.reserve(256);
}

void PatternPlayer::process(const Pattern& pattern, const Transport& transport, int64_t blockStart, int numSamples,
                            juce::MidiBuffer& midi, int channel) {
    const int64_t blockEnd = blockStart + numSamples;
    const double loopLength = pattern.lengthInBeats();
    noteOns.clear();

    // Note-ons: every loop repetition that overlaps this block, with one repetition of margin on
    // either side so rounding at the block edges can't drop a note.
    if (loopLength > 0.0 && !pattern.notes.empty()) {
        const auto firstLoop = static_cast<int64_t>(std::floor(transport.beatAt(blockStart) / loopLength)) - 1;
        const auto lastLoop = static_cast<int64_t>(std::floor(transport.beatAt(blockEnd) / loopLength)) + 1;

        for (int64_t loop = juce::jmax<int64_t>(0, firstLoop); loop <= lastLoop; loop++) {
            for (const auto& note : pattern.notes) {
                if (note.start < 0.0 || note.start >= loopLength) {
                    continue;
                }
                const double startBeat = static_cast<double>(loop) * loopLength + note.start;
                const int64_t sample = transport.sampleAt(startBeat);
                if (sample >= blockStart && sample < blockEnd) {
                    noteOns.push_back({ sample, note.pitch, note.velocity,
                                        startBeat + juce::jmax(minimumNoteLength, note.length) });
                }
            }
        }
    }

    std::stable_sort(noteOns.begin(), noteOns.end(),
                     [](const NoteOn& a, const NoteOn& b) { return a.sample < b.sample; });

    // MidiBuffer keeps insertion order for events on the same sample, so emitting in time order
    // with offs first gives off-before-on everywhere.
    for (const auto& on : noteOns) {
        releaseEndingBefore(on.sample, true, transport, blockStart, midi, channel);

        // Retriggering a pitch that is still sounding cuts the old note.
        for (auto it = sounding.begin(); it != sounding.end();) {
            if (it->pitch == on.pitch) {
                midi.addEvent(juce::MidiMessage::noteOff(channel, it->pitch), static_cast<int>(on.sample - blockStart));
                it = sounding.erase(it);
            } else {
                ++it;
            }
        }

        midi.addEvent(juce::MidiMessage::noteOn(channel, on.pitch, on.velocity),
                      static_cast<int>(on.sample - blockStart));
        sounding.push_back({ on.pitch, on.sample, on.endBeat });
    }

    releaseEndingBefore(blockEnd, false, transport, blockStart, midi, channel);
}

void PatternPlayer::releaseEndingBefore(int64_t limit, bool inclusive, const Transport& transport,
                                        int64_t blockStart, juce::MidiBuffer& midi, int channel) {
    for (;;) {
        // Release the earliest-ending note first so offs stay in time order.
        auto earliest = sounding.end();
        int64_t earliestEnd = 0;

        for (auto it = sounding.begin(); it != sounding.end(); ++it) {
            const int64_t end = juce::jmax(transport.sampleAt(it->endBeat), it->startSample + 1, blockStart);
            if (earliest == sounding.end() || end < earliestEnd) {
                earliest = it;
                earliestEnd = end;
            }
        }

        if (earliest == sounding.end() || earliestEnd > limit || (!inclusive && earliestEnd == limit)) {
            return;
        }

        midi.addEvent(juce::MidiMessage::noteOff(channel, earliest->pitch), static_cast<int>(earliestEnd - blockStart));
        sounding.erase(earliest);
    }
}

void PatternPlayer::releaseAll(juce::MidiBuffer& midi, int samplePosition, int channel) {
    for (const auto& note : sounding) {
        midi.addEvent(juce::MidiMessage::noteOff(channel, note.pitch), samplePosition);
    }
    sounding.clear();
}
