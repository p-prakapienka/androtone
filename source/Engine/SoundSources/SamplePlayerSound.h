#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

// A simple one-shot sample player sound, built on JUCE's Sampler.
//
// It decodes an audio file held in memory (e.g. an embedded BinaryData resource)
// and plays it back, repitched per MIDI note relative to a root note. Like the
// other sound sources (SineSound, SawSound) it is filtered to a single MIDI
// channel so it can be routed to one sequencer track.
//
// Pair it with SamplePlayerVoice (which makes playback genuinely one-shot -
// it ignores note-off and lets the sample play to its end):
//
//   synth.addSound(SamplePlayerSound::fromMemory(channel,
//                                                BinaryData::kick_wav,
//                                                BinaryData::kick_wavSize));
//   synth.addVoice(new SamplePlayerVoice());
//
// To supply the embedded sample you still need to, one time:
//   1. Add the .wav to the project (Projucer resource, or juce_add_binary_data
//      in Builds/desktop/CMakeLists.txt) so BinaryData symbols are generated.
//   2. #include "BinaryData.h" where you wire the sound up (PluginProcessor).
class SamplePlayerSound : public juce::SamplerSound {
public:
    SamplePlayerSound(
        int channel,
        const juce::String& name,
        juce::AudioFormatReader& source,
        const juce::BigInteger& midiNotes,
        int rootMidiNote,
        double attackTimeSecs,
        double releaseTimeSecs,
        double maxSampleLengthSeconds
    ) :
        juce::SamplerSound(
            name,
            source,
            midiNotes,
            rootMidiNote,
            attackTimeSecs,
            releaseTimeSecs,
            maxSampleLengthSeconds
        ),
        channel(channel) {}

    bool appliesToChannel(int ch) override { return ch == channel; }

    // Builds a one-shot sample player from an in-memory audio file, such as an
    // embedded BinaryData resource. The data must decode to a valid audio stream;
    // a failure means the embedded sample is missing or corrupt (a broken build),
    // so it asserts rather than returning a null sound.
    // The returned sound is owned by the Synthesiser once passed to addSound().
    static SamplePlayerSound* fromMemory(
        int channel,
        const void* data,
        size_t dataSize,
        int rootMidiNote = 60,
        double maxSampleLengthSeconds = 30.0
    ) {
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        auto stream = std::make_unique<juce::MemoryInputStream>(data, dataSize, false);
        std::unique_ptr<juce::AudioFormatReader> reader(
            formatManager.createReaderFor(std::move(stream))
        );

        // Fail fast: an embedded sample should always decode.
        jassert(reader != nullptr);

        juce::BigInteger allNotes;
        allNotes.setRange(0, 128, true);

        // Zero attack/release: the envelope is unused because SamplePlayerVoice
        // ignores note-off and plays the sample to completion.
        return new SamplePlayerSound(
            channel,
            "sample",
            *reader,
            allNotes,
            rootMidiNote,
            0.0,
            0.0,
            maxSampleLengthSeconds
        );
    }

private:
    int channel;
};
