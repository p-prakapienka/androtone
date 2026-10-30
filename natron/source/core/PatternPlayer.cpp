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

void PatternPlayer::process(const Pattern& pattern, double originBeat, const Transport& transport,
                            int64_t fromSample, int64_t toSample, int64_t blockStart, juce::MidiBuffer& midi,
                            int channel, float swing) {
    const double loopLength = pattern.lengthInBeats();
    noteOns.clear();

    // Note-ons: every loop repetition that overlaps the range, with one repetition of margin on
    // either side so rounding at the range edges can't drop a note.
    if (loopLength > 0.0 && !pattern.notes.empty() && toSample > fromSample) {
        const auto firstLoop =
            static_cast<int64_t>(std::floor((transport.beatAt(fromSample) - originBeat) / loopLength)) - 1;
        const auto lastLoop =
            static_cast<int64_t>(std::floor((transport.beatAt(toSample) - originBeat) / loopLength)) + 1;

        for (int64_t loop = juce::jmax<int64_t>(0, firstLoop); loop <= lastLoop; loop++) {
            for (const auto& note : pattern.notes) {
                if (note.start < 0.0 || note.start >= loopLength) {
                    continue;
                }
                const double startBeat =
                    originBeat + static_cast<double>(loop) * loopLength + note.start + swingOffset(note.start, swing);
                const int64_t sample = transport.sampleAt(startBeat);
                if (sample >= fromSample && sample < toSample) {
                    noteOns.push_back({ sample, note.pitch, note.velocity,
                                        startBeat + juce::jmax(minimumNoteLength, note.length) });
                }
            }
        }
    }

    // std::sort, not stable_sort: stable_sort may allocate a temporary buffer.
    std::sort(noteOns.begin(), noteOns.end(), [](const NoteOn& a, const NoteOn& b) {
        return a.sample != b.sample ? a.sample < b.sample : a.pitch < b.pitch;
    });

    // MidiBuffer keeps insertion order for events on the same sample, so emitting in time order
    // with offs first gives off-before-on everywhere.
    for (const auto& on : noteOns) {
        releaseEndingBefore(on.sample, true, transport, fromSample, blockStart, midi, channel);

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

    releaseEndingBefore(toSample, false, transport, fromSample, blockStart, midi, channel);
}

double PatternPlayer::swingOffset(double start, float swing) {
    // An off-beat 1/16 sits at 0.25 within its 1/8 pair; swing moves it to `swing` of the pair.
    const double positionInPair = std::fmod(start, 2.0 * Pattern::stepLength);
    if (std::abs(positionInPair - Pattern::stepLength) > 1.0e-6) {
        return 0.0;
    }
    return (juce::jlimit(0.5, 0.75, static_cast<double>(swing)) - 0.5) * (2.0 * Pattern::stepLength);
}

void PatternPlayer::releaseEndingBefore(int64_t limit, bool inclusive, const Transport& transport,
                                        int64_t rangeStart, int64_t blockStart, juce::MidiBuffer& midi,
                                        int channel) {
    for (;;) {
        // Release the earliest-ending note first so offs stay in time order.
        auto earliest = sounding.end();
        int64_t earliestEnd = 0;

        for (auto it = sounding.begin(); it != sounding.end(); ++it) {
            const int64_t end = juce::jmax(transport.sampleAt(it->endBeat), it->startSample + 1, rangeStart);
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
