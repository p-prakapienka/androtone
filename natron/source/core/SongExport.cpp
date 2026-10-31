#include "SongExport.h"

#include "Engine.h"
#include "Session.h"
#include "machines/DrumMachine.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <iterator>

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

namespace {
    // Adds `pattern` looping from `startBeat` until `endBeat`.
    void lay(const Pattern& pattern, double startBeat, double endBeat, std::vector<Note>& notes) {
        const double length = pattern.lengthInBeats();
        for (double loopStart = startBeat; loopStart < endBeat - 1.0e-9; loopStart += length) {
            for (const auto& note : pattern.notes) {
                const double start = loopStart + note.start;
                if (note.start >= length - 1.0e-9 || start >= endBeat - 1.0e-9) {
                    continue; // hidden past the pattern's end, or past the block's
                }
                notes.push_back({ start, std::min(note.length, endBeat - start), note.pitch, note.velocity });
            }
        }
    }
}

std::vector<std::vector<Note>> SongExport::playedNotes(const Song& song, const Options& options) {
    std::vector<std::vector<Note>> result;
    for (const auto& machine : song.machines) {
        std::vector<Note> notes;
        if (options.range == Options::Range::pattern) {
            lay(machine.patterns[static_cast<size_t>(machine.selectedPattern)], 0.0,
                options.patternBars * static_cast<double>(Transport::beatsPerBar), notes);
        } else {
            for (const auto& block : machine.blocks) {
                lay(machine.patterns[static_cast<size_t>(block.pattern)], block.startBar * static_cast<double>(Transport::beatsPerBar),
                    block.endBar() * static_cast<double>(Transport::beatsPerBar), notes);
            }
        }
        std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
        result.push_back(std::move(notes));
    }
    return result;
}

juce::Result SongExport::writeMidi(const Song& song, const Options& options, juce::OutputStream& stream) {
    const auto played = playedNotes(song, options);
    bool anyNotes = false;
    for (const auto& notes : played) {
        anyNotes = anyNotes || !notes.empty();
    }
    if (!anyNotes) {
        return juce::Result::fail("Nothing to export: no notes in the " + juce::String(options.range == Options::Range::song ? "song" : "selected patterns"));
    }

    const auto ticks = [](double beats) { return std::round(beats * midiTicksPerBeat); };
    juce::MidiFile file;
    file.setTicksPerQuarterNote(midiTicksPerBeat);

    juce::MidiMessageSequence conductor;
    conductor.addEvent(juce::MidiMessage::textMetaEvent(3, "Natron"), 0.0);
    conductor.addEvent(juce::MidiMessage::tempoMetaEvent(juce::roundToInt(60000000.0 / song.tempo)), 0.0);
    conductor.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0.0);
    file.addTrack(conductor);

    for (size_t m = 0; m < played.size(); m++) {
        const auto& machine = song.machines[m];
        // Channel 10 is the drum channel by convention; the others take 1-9 and 11-16 in rack order.
        static constexpr int melodicChannels[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16 };
        const int channel = machine.type == DrumMachine::typeName ? 10 : melodicChannels[m % std::size(melodicChannels)];
        juce::MidiMessageSequence track;
        track.addEvent(juce::MidiMessage::textMetaEvent(3, machine.name), 0.0);
        for (const auto& note : played[m]) {
            const auto velocity = static_cast<juce::uint8>(juce::jlimit(1, 127, juce::roundToInt(note.velocity * 127.0f)));
            track.addEvent(juce::MidiMessage::noteOn(channel, note.pitch, velocity), ticks(note.start));
            track.addEvent(juce::MidiMessage::noteOff(channel, note.pitch), ticks(note.start + note.length));
        }
        track.updateMatchedPairs();
        file.addTrack(track);
    }
    return file.writeTo(stream) ? juce::Result::ok() : juce::Result::fail("Could not write the MIDI file");
}
