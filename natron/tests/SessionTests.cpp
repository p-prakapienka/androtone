// Live editing through Session while the engine plays.

#include "TestHelpers.h"
#include "TestMachines.h"
#include "core/DemoSong.h"
#include "core/MachineRegistry.h"

#include <catch2/catch_test_macros.hpp>

namespace {
    constexpr int64_t step = 6000;
    constexpr int64_t bar = 96000;

    RecordingMachine* addRecorder(Rig& rig) {
        RecordingMachine::registerType();
        rig.session.addMachine(RecordingMachine::typeName);
        rig.render(1);
        auto* recorder = RecordingMachine::instances().back();
        recorder->events.clear();
        recorder->position = 0;
        return recorder;
    }
}

TEST_CASE("Editing the playing pattern takes effect on the next block", "[session][live]") {
    Rig rig;
    auto* recorder = addRecorder(rig);
    rig.session.setPlaying(true);
    rig.render(step);

    Pattern pattern;
    pattern.notes.push_back({ 1.0, 0.25, 64, 0.8f }); // beat 1, still ahead of the playhead
    rig.session.setPattern(0, 0, pattern);
    rig.render(bar);

    REQUIRE(recorder->noteOns().size() == 1);
    CHECK(recorder->noteOns()[0].sample == 4 * step);
    CHECK(recorder->noteOns()[0].pitch == 64);
}

TEST_CASE("Machines can be added and removed while playing", "[session][live]") {
    RecordingMachine::registerType();
    Rig rig;
    REQUIRE(rig.session.loadSong(makeDemoSong()).wasOk());
    rig.session.setPlaying(true);
    rig.render(bar / 2);

    const int destroyedBefore = RecordingMachine::destroyed();
    const int added = rig.session.addMachine(RecordingMachine::typeName, "Probe");
    CHECK(added == 2);
    rig.render(512);
    CHECK(rig.engine.getNumMachines() == 3);

    CHECK(rig.session.removeMachine(0)); // the bass; chords and probe move up
    rig.render(512);
    CHECK(rig.engine.getNumMachines() == 2);
    CHECK(rig.session.getSong().machines[0].name == "Chords");
    CHECK(rig.session.getSong().machines[1].name == "Probe");

    CHECK(rig.session.removeMachine(1));
    rig.render(512);
    CHECK(RecordingMachine::destroyed() == destroyedBefore); // not freed on the audio thread...
    rig.session.collectGarbage();
    CHECK(RecordingMachine::destroyed() == destroyedBefore + 1); // ...but by the message thread

    const auto stats = analyse(rig.render(bar));
    CHECK(stats.allFinite);
    CHECK(stats.rms > 1.0e-4); // the chords play on
}

TEST_CASE("Volumes follow their machine when an earlier one is removed", "[session][mixer]") {
    Rig rig;
    rig.session.addMachine(TestToneMachine::typeName);
    rig.session.addMachine(TestToneMachine::typeName);
    rig.session.setMachineVolume(1, 0.25f);
    rig.render(512);

    rig.session.removeMachine(0);
    rig.render(512);

    CHECK(juce::exactlyEqual(rig.engine.getMachineVolume(0), 0.25f));
    CHECK(juce::exactlyEqual(rig.session.getSong().machines[0].volume, 0.25f));
}

TEST_CASE("Parameters are clamped and reach the machine", "[session][parameters]") {
    Rig rig;
    const int m = rig.session.addMachine(TestToneMachine::typeName);

    CHECK(rig.session.setParameter(m, "level", 0.5f));
    CHECK(juce::exactlyEqual(rig.session.getParameter(m, "level"), 0.5f));
    CHECK(rig.session.setParameter(m, "level", 7.0f));
    CHECK(juce::exactlyEqual(rig.session.getParameter(m, "level"), 1.0f));
    CHECK_FALSE(rig.session.setParameter(m, "no-such-parameter", 1.0f));

    const auto* parameters = rig.session.getParameters(m);
    REQUIRE(parameters != nullptr);
    CHECK(juce::exactlyEqual(parameters->get(parameters->indexOf("level")), 1.0f));
}

TEST_CASE("A parameter changes the sound", "[session][parameters]") {
    Rig rig;
    const int m = rig.session.addMachine(TestToneMachine::typeName);
    Pattern pattern;
    pattern.notes.push_back({ 0.0, 4.0, 69, 1.0f });
    rig.session.setPattern(m, 0, pattern);

    rig.session.setParameter(m, "level", 0.0f);
    rig.session.setPlaying(true);
    CHECK(analyse(rig.render(24000)).peak <= 0.0f);
}

TEST_CASE("Invalid edits are rejected", "[session]") {
    Rig rig;
    CHECK(rig.session.addMachine("NoSuchMachine") == -1);
    CHECK_FALSE(rig.session.removeMachine(0));
    CHECK_FALSE(rig.session.selectPattern(0, 0));

    const int m = rig.session.addMachine(TestToneMachine::typeName);
    Pattern threeBars;
    threeBars.bars = 3;
    CHECK_FALSE(rig.session.setPattern(m, 0, threeBars));
    CHECK_FALSE(rig.session.setPattern(m, 64, Pattern {}));
    CHECK_FALSE(rig.session.selectPattern(m, -1));
}

TEST_CASE("Loading a song replaces the rack and stops playback", "[session][load]") {
    Rig rig;
    for (int i = 0; i < 5; i++) {
        rig.session.addMachine(TestToneMachine::typeName);
    }
    rig.session.setPlaying(true);
    rig.render(bar);

    REQUIRE(rig.session.loadSong(makeDemoSong()).wasOk());
    CHECK_FALSE(rig.session.isPlaying());
    rig.render(512);

    CHECK(rig.engine.getNumMachines() == 2);
    CHECK(rig.session.getNumMachines() == 2);
    CHECK(juce::exactlyEqual(rig.engine.getTempo(), 110.0));
}

TEST_CASE("A song with an unknown machine type is rejected without changes", "[session][load]") {
    Rig rig;
    REQUIRE(rig.session.loadSong(makeDemoSong()).wasOk());

    auto broken = makeDemoSong();
    broken.machines[1].type = "FromTheFuture";
    const auto result = rig.session.loadSong(broken);

    CHECK(result.failed());
    CHECK(result.getErrorMessage().contains("FromTheFuture"));
    CHECK(rig.session.getNumMachines() == 2);
    CHECK(rig.session.getSong().machines[1].type == TestToneMachine::typeName);
}

TEST_CASE("Loading keeps known parameters and drops unknown ones", "[session][load]") {
    Rig rig;
    auto song = makeDemoSong();
    song.machines[0].parameters["level"] = 0.9f;
    song.machines[0].parameters["retired-parameter"] = 3.0f;
    REQUIRE(rig.session.loadSong(song).wasOk());

    const auto& loaded = rig.session.getSong().machines[0].parameters;
    CHECK(juce::exactlyEqual(loaded.at("level"), 0.9f));
    CHECK(loaded.count("retired-parameter") == 0);
    CHECK(loaded.count("attack") == 1); // filled in with the default
}

TEST_CASE("The registry lists and creates the built-in machines", "[registry]") {
    CHECK(MachineRegistry::getTypeNames().contains(TestToneMachine::typeName));
    auto machine = MachineRegistry::create(TestToneMachine::typeName);
    REQUIRE(machine != nullptr);
    CHECK(machine->getTypeName() == TestToneMachine::typeName);
    CHECK(MachineRegistry::create("NoSuchMachine") == nullptr);
}
