// Offline WAV and MIDI export.

#include "TestHelpers.h"
#include "core/DemoSong.h"
#include "core/SongExport.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <juce_audio_formats/juce_audio_formats.h>
#include <tuple>

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

namespace {
    Pattern notesAt(int bars, std::initializer_list<Note> notes) {
        Pattern pattern;
        pattern.bars = bars;
        pattern.notes = notes;
        return pattern;
    }

    MachineState machineState(const juce::String& type, const juce::String& name) {
        MachineState state;
        state.type = type;
        state.name = name;
        return state;
    }

    juce::MidiFile readMidi(const juce::MemoryOutputStream& written) {
        juce::MemoryInputStream input(written.getData(), written.getDataSize(), false);
        juce::MidiFile file;
        REQUIRE(file.readFrom(input));
        return file;
    }
}

TEST_CASE("Song-mode MIDI notes follow the blocks", "[export][midi]") {
    Song song;
    MachineState lead;
    lead.type = "Subtractive";
    lead.name = "Lead";
    lead.patterns[0] = notesAt(1, { { 0.0, 1.0, 60, 0.8f }, { 3.5, 1.0, 62, 0.5f }, { 5.0, 0.5, 70, 1.0f } }); // 70 is hidden past the end
    lead.patterns[1] = notesAt(2, { { 6.0, 0.5, 64, 0.8f } });
    lead.blocks = { { 0, 2, 0 }, { 3, 1, 1 } }; // A01 loops twice; A02 (2 bars) cut to its first bar
    song.machines.push_back(lead);

    SongExport::Options options;
    const auto played = SongExport::playedNotes(song, options);
    REQUIRE(played.size() == 1);
    const std::vector<Note> expected {
        { 0.0, 1.0, 60, 0.8f }, { 3.5, 1.0, 62, 0.5f }, // rings on into the second loop, inside the block
        { 4.0, 1.0, 60, 0.8f }, { 7.5, 0.5, 62, 0.5f }, // cut at the block's end
    };                                                  // A02's only note falls past its 1-bar block
    CHECK(played[0] == expected);
}

TEST_CASE("Pattern-mode MIDI export loops the selected patterns", "[export][midi]") {
    Song song;
    MachineState bass;
    bass.type = "Acid";
    bass.name = "Bass";
    bass.selectedPattern = 5;
    bass.patterns[5] = notesAt(1, { { 1.0, 0.25, 36, 1.0f } });
    song.machines.push_back(bass);
    song.machines.push_back(machineState("Drums", "Beat")); // no notes: an empty track

    SongExport::Options options;
    options.range = SongExport::Options::Range::pattern;
    options.patternBars = 3;
    const auto played = SongExport::playedNotes(song, options);
    REQUIRE(played[0].size() == 3);
    CHECK(played[0][2] == Note { 9.0, 0.25, 36, 1.0f });
    CHECK(played[1].empty());
}

TEST_CASE("MIDI files have a tempo track and one track per machine", "[export][midi]") {
    Song song;
    song.tempo = 125.0;
    auto drums = machineState("Drums", "Beat");
    drums.patterns[0] = notesAt(1, { { 0.0, 0.25, 36, 1.0f }, { 1.0, 0.25, 38, 0.5f } });
    drums.blocks = { { 0, 1, 0 } };
    auto pad = machineState("Subtractive", "Pad");
    pad.patterns[0] = notesAt(1, { { 2.0, 2.0, 67, 0.8f } });
    pad.blocks = { { 1, 1, 0 } };
    song.machines = { drums, pad };

    juce::MemoryOutputStream written;
    REQUIRE(SongExport::writeMidi(song, {}, written).wasOk());
    const auto file = readMidi(written);
    CHECK(file.getTimeFormat() == SongExport::midiTicksPerBeat);
    REQUIRE(file.getNumTracks() == 3);

    juce::MidiMessageSequence tempos;
    file.findAllTempoEvents(tempos);
    REQUIRE(tempos.getNumEvents() == 1);
    CHECK_THAT(60.0 / tempos.getEventPointer(0)->message.getTempoSecondsPerQuarterNote(), Catch::Matchers::WithinAbs(125.0, 0.01));

    std::vector<std::tuple<double, int, int, int>> ons; // tick, channel, pitch, velocity
    juce::StringArray names;
    for (int t = 1; t < file.getNumTracks(); t++) {
        for (const auto* event : *file.getTrack(t)) {
            const auto& message = event->message;
            if (message.isTrackNameEvent()) {
                names.add(message.getTextFromTextMetaEvent());
            }
            if (message.isNoteOn()) {
                ons.emplace_back(message.getTimeStamp(), message.getChannel(), message.getNoteNumber(), message.getVelocity());
            }
        }
    }
    CHECK(names == juce::StringArray { "Beat", "Pad" });
    REQUIRE(ons.size() == 3);
    CHECK(ons[0] == std::make_tuple(0.0, 10, 36, 127));
    CHECK(ons[1] == std::make_tuple(960.0, 10, 38, 64));
    CHECK(ons[2] == std::make_tuple(4.0 * 960 + 2.0 * 960, 2, 67, 102)); // bar 2 beat 3; channel = rack position
}

TEST_CASE("A song without notes is not exported to MIDI", "[export][midi]") {
    Song song;
    song.machines.push_back(machineState("Subtractive", "Empty"));
    juce::MemoryOutputStream written;
    CHECK(SongExport::writeMidi(song, {}, written).failed());
}
