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
}
