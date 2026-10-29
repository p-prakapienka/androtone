#pragma once

#include "Pattern.h"
#include "Transport.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <cstdint>
#include <vector>

// Turns a looping Pattern into sample-accurate MIDI for one machine. Sounding notes are tracked by
// the beat they end on, so a note can outlast its step, the block, or the pattern loop, and a tempo
// change mid-note stretches what's left of it musically.
class PatternPlayer {
public:
    PatternPlayer();

    // Writes the note-ons and note-offs falling in [fromSample, toSample) into `midi`, at positions
    // relative to `blockStart`. The pattern loops from `originBeat` (where it started playing).
    // A note-off always comes before a note-on on the same sample. Calling this for consecutive
    // ranges, with a different pattern for each, is how the engine switches patterns mid-block.
    void process(const Pattern& pattern, double originBeat, const Transport& transport, int64_t fromSample,
                 int64_t toSample, int64_t blockStart, juce::MidiBuffer& midi, int channel = 1);

    // Ends every sounding note at block-relative sample `samplePosition` (transport stop).
    void releaseAll(juce::MidiBuffer& midi, int samplePosition, int channel = 1);

    int getNumSoundingNotes() const { return static_cast<int>(sounding.size()); }

private:
    struct SoundingNote {
        int pitch;
        int64_t startSample;
        double endBeat;
    };

    struct NoteOn {
        int64_t sample;
        int pitch;
        float velocity;
        double endBeat;
    };

    std::vector<SoundingNote> sounding;
    std::vector<NoteOn> noteOns;

    // Emits note-offs, in time order, for sounding notes that end before `limit`
    // (or at it, when `inclusive`).
    void releaseEndingBefore(int64_t limit, bool inclusive, const Transport& transport, int64_t rangeStart,
                             int64_t blockStart, juce::MidiBuffer& midi, int channel);
};
