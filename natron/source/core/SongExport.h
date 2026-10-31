#pragma once

#include "Song.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>

// Offline rendering to audio and WAV. Renders on its own Engine + Session built from a copy of the
// song, so it never touches the live engine and can run on a background thread.
namespace SongExport {
    struct Options {
        enum class Range {
            song,    // the timeline from bar 1 to the end of the last block (loop ignored)
            pattern, // the selected patterns, for `patternBars` bars
        };

        Range range = Range::song;
        int patternBars = 4;
        double sampleRate = 44100.0;
        int bitDepth = 16;         // 16 or 24
        double tailSeconds = 3.0;  // rendered after the end, for releases, delay and reverb
        int blockSize = 512;
    };

    // Progress callback: 0..1; return false to cancel.
    using Progress = std::function<bool(double)>;

    // Renders into `audio` (stereo, resized to fit). Fails on an empty song or cancellation.
    juce::Result render(const Song& song, const Options& options, juce::AudioBuffer<float>& audio,
                        const Progress& progress = {});

    juce::Result writeWav(const juce::AudioBuffer<float>& audio, double sampleRate, int bitDepth,
                          juce::OutputStream& stream);

    // Length of the musical part (without the tail), in samples.
    int64_t lengthInSamples(const Song& song, const Options& options);

    // MIDI export: the notes each machine plays over the same range as `render` (song timeline or
    // looping selected patterns), without swing, one track per machine (Drums on channel 10).
    static constexpr int midiTicksPerBeat = 960;

    // Each machine's notes as played, in beats from the start of the range (sorted by start).
    // A pattern loops within its block; notes are cut at the block's (or the range's) end.
    std::vector<std::vector<Note>> playedNotes(const Song& song, const Options& options);

    // A type 1 MIDI file: track 1 holds tempo and 4/4, then one named track per machine. Fails if
    // there are no notes.
    juce::Result writeMidi(const Song& song, const Options& options, juce::OutputStream& stream);
}
