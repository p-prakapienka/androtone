#include "SongExport.h"

#include "Engine.h"
#include "Session.h"

#include <juce_audio_formats/juce_audio_formats.h>

int64_t SongExport::lengthInSamples(const Song& song, const Options& options) {
    const int bars = options.range == Options::Range::song ? song.endBar() : options.patternBars;
    const double beats = bars * static_cast<double>(Transport::beatsPerBar);
    return static_cast<int64_t>(std::ceil(beats * 60.0 / song.tempo * options.sampleRate));
}

juce::Result SongExport::render(const Song& song, const Options& options, juce::AudioBuffer<float>& audio,
                                const Progress& progress) {
    const int64_t musicLength = lengthInSamples(song, options);
    if (musicLength <= 0) {
        return juce::Result::fail(options.range == Options::Range::song ? "The song timeline is empty"
                                                                        : "Nothing to render");
    }
    const int64_t totalLength = musicLength + static_cast<int64_t>(options.tailSeconds * options.sampleRate);
    if (totalLength > std::numeric_limits<int>::max()) {
        return juce::Result::fail("Song is too long to export");
    }

    Engine engine;
    Session session(engine);
    Song copy = song;
    copy.songMode = options.range == Options::Range::song;
    copy.loopEnabled = false; // export plays the song through once
    if (const auto loaded = session.loadSong(copy); loaded.failed()) {
        return loaded;
    }
    engine.prepare(options.sampleRate, options.blockSize);
    session.setPlaying(true);

    audio.setSize(2, static_cast<int>(totalLength));
    audio.clear();
    juce::AudioBuffer<float> block(2, options.blockSize);

    for (int64_t position = 0; position < totalLength; position += options.blockSize) {
        const int numSamples = static_cast<int>(juce::jmin<int64_t>(options.blockSize, totalLength - position));
        if (position >= musicLength) {
            session.setPlaying(false); // pattern exports stop here; song exports have stopped themselves
        }
        block.setSize(2, numSamples, false, false, true);
        engine.process(block);
        for (int ch = 0; ch < 2; ch++) {
            audio.copyFrom(ch, static_cast<int>(position), block, ch, 0, numSamples);
        }
        session.collectGarbage();

        if (progress && !progress(static_cast<double>(position + numSamples) / static_cast<double>(totalLength))) {
            return juce::Result::fail("Export cancelled");
        }
    }
    return juce::Result::ok();
}

juce::Result SongExport::writeWav(const juce::AudioBuffer<float>& audio, double sampleRate, int bitDepth,
                                  juce::OutputStream& stream) {
    if (bitDepth != 16 && bitDepth != 24) {
        return juce::Result::fail("Unsupported bit depth: " + juce::String(bitDepth));
    }
    juce::WavAudioFormat wav;
    // The writer takes ownership of the stream it's given, so hand it a non-owning wrapper.
    struct Borrowed : juce::OutputStream {
        explicit Borrowed(juce::OutputStream& target) : out(target) {}
        void flush() override { out.flush(); }
        bool setPosition(juce::int64 p) override { return out.setPosition(p); }
        juce::int64 getPosition() override { return out.getPosition(); }
        bool write(const void* data, size_t size) override { return out.write(data, size); }
        juce::OutputStream& out;
    };
    std::unique_ptr<juce::OutputStream> borrowed = std::make_unique<Borrowed>(stream);
    const auto writer = wav.createWriterFor(
        borrowed, juce::AudioFormatWriterOptions {}.withSampleRate(sampleRate).withNumChannels(2).withBitsPerSample(bitDepth));
    if (writer == nullptr) {
        return juce::Result::fail("Could not create a WAV writer");
    }
    if (!writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples())) {
        return juce::Result::fail("Writing the WAV data failed");
    }
    return juce::Result::ok();
}
