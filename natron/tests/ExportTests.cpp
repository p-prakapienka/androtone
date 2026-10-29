// Offline WAV export.

#include "TestHelpers.h"
#include "core/DemoSong.h"
#include "core/SongExport.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

TEST_CASE("Song export covers the timeline plus the tail", "[export]") {
    const auto song = makeDemoSong();
    REQUIRE(song.endBar() > 0);

    SongExport::Options options;
    options.sampleRate = 48000.0;
    options.tailSeconds = 1.0;
    juce::AudioBuffer<float> audio;
    REQUIRE(SongExport::render(song, options, audio).wasOk());

    const int64_t music = SongExport::lengthInSamples(song, options);
    CHECK(music == static_cast<int64_t>(std::ceil(song.endBar() * 4 * 60.0 / song.tempo * 48000.0)));
    CHECK(audio.getNumSamples() == music + 48000);
    const auto stats = analyse(audio);
    CHECK(stats.allFinite);
    CHECK(stats.rms > 1.0e-3);
    CHECK(stats.peak <= 1.0f);
}

TEST_CASE("Export doesn't depend on the live session or its playback mode", "[export]") {
    auto song = makeDemoSong();
    SongExport::Options options;
    options.tailSeconds = 0.5;
    juce::AudioBuffer<float> first, second;
    REQUIRE(SongExport::render(song, options, first).wasOk());
    song.songMode = false;
    song.loopEnabled = true; // ignored: an export plays the song once
    REQUIRE(SongExport::render(song, options, second).wasOk());

    REQUIRE(first.getNumSamples() == second.getNumSamples());
    float difference = 0.0f;
    for (int i = 0; i < first.getNumSamples(); i++) {
        difference = std::max(difference, std::abs(first.getSample(0, i) - second.getSample(0, i)));
    }
    CHECK(difference < 1.0e-6f);
}

TEST_CASE("Pattern export renders the selected patterns for N bars", "[export]") {
    SongExport::Options options;
    options.range = SongExport::Options::Range::pattern;
    options.patternBars = 2;
    options.tailSeconds = 0.0;
    juce::AudioBuffer<float> audio;
    REQUIRE(SongExport::render(makeDemoSong(), options, audio).wasOk());
    CHECK(audio.getNumSamples() == SongExport::lengthInSamples(makeDemoSong(), options));
    CHECK(analyse(audio).rms > 1.0e-3);
}

TEST_CASE("Exporting an empty song fails, and cancelling stops early", "[export]") {
    juce::AudioBuffer<float> audio;
    CHECK(SongExport::render(Song {}, {}, audio).failed());

    int calls = 0;
    const auto result = SongExport::render(makeDemoSong(), {}, audio, [&](double) { return ++calls < 5; });
    CHECK(result.failed());
    CHECK(result.getErrorMessage().contains("cancel"));
    CHECK(calls == 5);
}

TEST_CASE("Exported WAV files read back", "[export]") {
    const int bits = GENERATE(16, 24);
    CAPTURE(bits);
    SongExport::Options options;
    options.tailSeconds = 0.2;
    juce::AudioBuffer<float> audio;
    REQUIRE(SongExport::render(makeDemoSong(), options, audio).wasOk());

    juce::MemoryOutputStream out;
    REQUIRE(SongExport::writeWav(audio, options.sampleRate, bits, out).wasOk());

    juce::WavAudioFormat wav;
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        wav.createReaderFor(new juce::MemoryInputStream(out.getData(), out.getDataSize(), false), true));
    REQUIRE(reader != nullptr);
    CHECK(reader->numChannels == 2);
    CHECK(reader->bitsPerSample == static_cast<unsigned int>(bits));
    CHECK(juce::exactlyEqual(reader->sampleRate, options.sampleRate));
    REQUIRE(reader->lengthInSamples == audio.getNumSamples());

    juce::AudioBuffer<float> back(2, audio.getNumSamples());
    reader->read(&back, 0, audio.getNumSamples(), 0, true, true);
    float difference = 0.0f;
    for (int i = 0; i < audio.getNumSamples(); i++) {
        difference = std::max(difference, std::abs(audio.getSample(1, i) - back.getSample(1, i)));
    }
    CHECK(difference < (bits == 16 ? 1.0e-4f : 1.0e-6f));

    CHECK(SongExport::writeWav(audio, options.sampleRate, 12, out).failed());
}
