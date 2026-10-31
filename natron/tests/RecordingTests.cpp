// Recording live notes into patterns: LiveRecorder pairs and quantises; the engine reports where
// each note fell in the playing pattern; Session writes takes into the song as one undo step.

#include "TestHelpers.h"
#include "core/LiveRecorder.h"
#include "core/machines/SubtractiveSynth.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

namespace {
    Engine::RecordedEvent on(double patternBeat, double beat, int pitch, float velocity = 0.8f, double length = 4.0) {
        return { 0, 3, patternBeat, beat, length, pitch, velocity };
    }
    Engine::RecordedEvent off(double beat, int pitch) {
        return { 0, 3, 0.0, beat, 4.0, pitch, 0.0f };
    }

    constexpr int64_t beatSamples = 24000; // 120 BPM at 48 kHz
}

TEST_CASE("The recorder pairs note-ons and note-offs into quantised notes", "[recording]") {
    LiveRecorder recorder;
    LiveRecorder::Recorded note;
    CHECK_FALSE(recorder.handle(on(1.07, 9.07, 60, 0.9f), note));
    CHECK_FALSE(recorder.handle(off(5.0, 61), note)); // never pressed while recording: ignored
    REQUIRE(recorder.handle(off(9.62, 60), note));
    CHECK(note.machine == 0);
    CHECK(note.pattern == 3);
    CHECK(note.note == Note { 1.0, 0.5, 60, 0.9f }); // start and length snap to 1/16
    CHECK_FALSE(recorder.isHolding());
}

TEST_CASE("Recorded notes near the loop point and across it", "[recording]") {
    LiveRecorder recorder;
    LiveRecorder::Recorded note;
    recorder.handle(on(3.95, 7.95, 62), note); // rounds onto the loop point: step 1
    REQUIRE(recorder.handle(off(8.2, 62), note));
    CHECK_THAT(note.note.start, WithinAbs(0.0, 1.0e-9));
    CHECK_THAT(note.note.length, WithinAbs(0.25, 1.0e-9));

    recorder.handle(on(3.5, 7.5, 64), note); // held across the loop point: keeps its length
    REQUIRE(recorder.handle(off(8.5, 64), note));
    CHECK_THAT(note.note.start, WithinAbs(3.5, 1.0e-9));
    CHECK_THAT(note.note.length, WithinAbs(1.0, 1.0e-9));

    recorder.handle(on(0.0, 0.0, 65), note); // held for longer than the pattern: one pattern long
    REQUIRE(recorder.handle(off(20.0, 65), note));
    CHECK_THAT(note.note.length, WithinAbs(4.0, 1.0e-9));
}

TEST_CASE("Unquantised recording keeps the timing as played", "[recording]") {
    LiveRecorder recorder;
    recorder.grid = 0.0;
    LiveRecorder::Recorded note;
    recorder.handle(on(1.07, 1.07, 60), note);
    REQUIRE(recorder.handle(off(1.3, 60), note));
    CHECK_THAT(note.note.start, WithinAbs(1.07, 1.0e-9));
    CHECK_THAT(note.note.length, WithinAbs(0.23, 1.0e-9));
}

TEST_CASE("Held notes end when recording stops", "[recording]") {
    LiveRecorder recorder;
    LiveRecorder::Recorded note;
    recorder.handle(on(2.0, 2.0, 60), note);
    recorder.handle(on(2.0, 2.0, 67), note);
    const auto finished = recorder.finishAll(3.0);
    REQUIRE(finished.size() == 2);
    CHECK(finished[0].note == Note { 2.0, 1.0, 60, 0.8f });
    CHECK(finished[1].note.pitch == 67);
    CHECK_FALSE(recorder.isHolding());
}

TEST_CASE("Playing while recording writes notes into the playing pattern", "[recording]") {
    MachineRig rig(SubtractiveSynth::typeName);
    rig.engine.setLiveTarget(rig.machine);
    rig.session.selectPattern(rig.machine, 2);
    rig.session.setRecording(true);
    rig.session.setPlaying(true);

    rig.render(beatSamples * 5); // bar 2, beat 2: pattern beat 1 (one-bar pattern)
    rig.engine.pushLiveMidi(juce::MidiMessage::noteOn(1, 64, 0.75f));
    rig.render(beatSamples / 2);
    rig.engine.pushLiveMidi(juce::MidiMessage::noteOff(1, 64));
    rig.engine.pushLiveMidi(juce::MidiMessage::noteOn(1, 67, 1.0f));
    rig.render(beatSamples * 3 / 4);
    rig.session.pollRecording();

    const auto& pattern = rig.session.getPattern(rig.machine, 2);
    REQUIRE(pattern.notes.size() == 1);
    CHECK_THAT(pattern.notes[0].start, WithinAbs(1.0, 1.0e-9));
    CHECK_THAT(pattern.notes[0].length, WithinAbs(0.5, 1.0e-9));
    CHECK(pattern.notes[0].pitch == 64);
    CHECK_THAT(pattern.notes[0].velocity, WithinAbs(0.75, 0.01));

    rig.session.setRecording(false); // 67 is still held: it ends here
    REQUIRE(rig.session.getPattern(rig.machine, 2).notes.size() == 2);
    const auto held = rig.session.getPattern(rig.machine, 2).notes[1];
    CHECK(held.pitch == 67);
    CHECK_THAT(held.start, WithinAbs(1.5, 1.0e-9));
    CHECK_THAT(held.length, WithinAbs(0.75, 1.0e-9));
    CHECK(rig.session.getPattern(rig.machine, 0).isEmpty());

    REQUIRE(rig.session.undo()); // the whole take is one step
    CHECK(rig.session.getPattern(rig.machine, 2).isEmpty());
}

TEST_CASE("Nothing is recorded when stopped or in song mode", "[recording]") {
    MachineRig rig(SubtractiveSynth::typeName);
    rig.engine.setLiveTarget(rig.machine);
    rig.session.setRecording(true);

    rig.engine.pushLiveMidi(juce::MidiMessage::noteOn(1, 60, 0.8f)); // stopped
    rig.render(beatSamples);
    rig.engine.pushLiveMidi(juce::MidiMessage::noteOff(1, 60));
    rig.render(beatSamples);

    rig.session.setSongMode(true);
    rig.session.setBlocks(rig.machine, { { 0, 4, 0 } });
    rig.session.setPlaying(true);
    rig.engine.pushLiveMidi(juce::MidiMessage::noteOn(1, 62, 0.8f));
    rig.render(beatSamples);
    rig.engine.pushLiveMidi(juce::MidiMessage::noteOff(1, 62));
    rig.render(beatSamples);
    rig.session.setRecording(false);

    CHECK(rig.session.getPattern(rig.machine, 0).isEmpty());
}

TEST_CASE("Stopping playback while recording ends held notes", "[recording]") {
    MachineRig rig(SubtractiveSynth::typeName);
    rig.engine.setLiveTarget(rig.machine);
    rig.session.setRecording(true);
    rig.session.setPlaying(true);
    rig.render(beatSamples);
    rig.engine.pushLiveMidi(juce::MidiMessage::noteOn(1, 60, 0.8f));
    rig.render(beatSamples * 2);
    rig.session.setPlaying(false);

    const auto& pattern = rig.session.getPattern(rig.machine, 0);
    REQUIRE(pattern.notes.size() == 1);
    CHECK_THAT(pattern.notes[0].start, WithinAbs(1.0, 1.0e-9));
    CHECK_THAT(pattern.notes[0].length, WithinAbs(2.0, 1.0e-9));
}
