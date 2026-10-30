#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

// An audio sample held in memory: imported from a file, or synthesised. Samples are stored inside
// song files (as base64 16-bit WAV), so a song stays self-contained on any device.
struct SampleData {
    static constexpr double maxSeconds = 30.0;

    juce::String name;
    juce::AudioBuffer<float> audio; // 1 or 2 channels
    double sampleRate = 48000.0;

    bool isEmpty() const { return audio.getNumSamples() == 0; }
    double lengthInSeconds() const { return audio.getNumSamples() / sampleRate; }

    // Reads WAV, AIFF, FLAC or Ogg Vorbis (up to maxSeconds; longer files are truncated).
    static juce::Result load(juce::InputStream& stream, const juce::String& name, SampleData& sample);

    // <Sample name="" sampleRate="" wav="base64..."/> and back.
    juce::ValueTree toTree() const;
    static juce::Result fromTree(const juce::ValueTree& tree, SampleData& sample);
};
