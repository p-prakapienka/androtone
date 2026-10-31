// Undo/redo through Session: every edit can be taken back, knob turns and drags are one step each,
// and undoing keeps playback going.

#include "TestHelpers.h"
#include "core/DemoSong.h"
#include "core/machines/AcidBass.h"
#include "core/machines/SubtractiveSynth.h"

#include <catch2/catch_test_macros.hpp>

namespace {
    juce::String songText(const Session& session) {
        juce::MemoryOutputStream stream;
        REQUIRE(session.saveToStream(stream).wasOk());
        return stream.toString();
    }

    // A rig with the demo song and a clock the test moves by hand.
    struct UndoRig : Rig {
        double now = 0.0;
        UndoRig() {
            session.clock = [this] { return now; };
            REQUIRE(session.loadSong(makeDemoSong()).wasOk());
        }
        void later() { now += 10.0; } // far enough apart not to merge
    };

    Pattern patternWith(std::initializer_list<int> pitches) {
        Pattern pattern;
        double start = 0.0;
        for (const int pitch : pitches) {
            pattern.notes.push_back({ start, 0.25, pitch, 0.8f });
            start += 0.5;
        }
        return pattern;
    }
}

TEST_CASE("Undo and redo a parameter change", "[undo]") {
    UndoRig rig;
    const float before = rig.session.getParameter(1, "cutoff");
    REQUIRE(rig.session.setParameter(1, "cutoff", 2000.0f));
    CHECK(rig.session.canUndo());

    REQUIRE(rig.session.undo());
    CHECK(juce::exactlyEqual(rig.session.getParameter(1, "cutoff"), before));
    const auto& live = *rig.session.getParameters(1);
    CHECK(juce::exactlyEqual(live.get(live.indexOf("cutoff")), before)); // the running machine too
    CHECK(rig.session.canRedo());

    REQUIRE(rig.session.redo());
    CHECK(juce::exactlyEqual(rig.session.getParameter(1, "cutoff"), 2000.0f));
    CHECK_FALSE(rig.session.canRedo());
}

TEST_CASE("Turning a knob is one undo step", "[undo]") {
    UndoRig rig;
    const float before = rig.session.getParameter(1, "cutoff");
    for (const float value : { 600.0f, 700.0f, 800.0f, 900.0f }) {
        rig.session.setParameter(1, "cutoff", value);
        rig.now += 0.2;
    }
    rig.session.setParameter(1, "resonance", 0.9f); // another knob: a new step
    rig.later();
    rig.session.setParameter(1, "cutoff", 1500.0f); // the same knob much later: a new step too

    REQUIRE(rig.session.undo());
    CHECK(juce::exactlyEqual(rig.session.getParameter(1, "cutoff"), 900.0f));
    REQUIRE(rig.session.undo());
    CHECK(juce::exactlyEqual(rig.session.getParameter(1, "cutoff"), 900.0f));
    CHECK_FALSE(juce::exactlyEqual(rig.session.getParameter(1, "resonance"), 0.9f));
    REQUIRE(rig.session.undo());
    CHECK(juce::exactlyEqual(rig.session.getParameter(1, "cutoff"), before));
    CHECK_FALSE(rig.session.canUndo());
}

TEST_CASE("A gesture is one undo step", "[undo]") {
    UndoRig rig;
    const auto original = rig.session.getPattern(1, 5);
    rig.session.beginGesture(); // e.g. dragging a note around
    rig.session.setPattern(1, 5, patternWith({ 40 }));
    rig.later();
    rig.session.setPattern(1, 5, patternWith({ 41 }));
    rig.session.setPattern(1, 5, patternWith({ 42 }));
    rig.session.endGesture();
    rig.session.setPattern(1, 5, patternWith({ 42, 47 })); // separate edit

    REQUIRE(rig.session.undo());
    CHECK(rig.session.getPattern(1, 5) == patternWith({ 42 }));
    REQUIRE(rig.session.undo());
    CHECK(rig.session.getPattern(1, 5) == original);
    CHECK_FALSE(rig.session.canUndo());
}

TEST_CASE("Pattern edits outside a gesture are separate steps", "[undo]") {
    UndoRig rig;
    rig.session.setPattern(1, 5, patternWith({ 40 }));
    rig.session.setPattern(1, 5, patternWith({ 40, 43 })); // same instant, still its own step
    REQUIRE(rig.session.undo());
    CHECK(rig.session.getPattern(1, 5) == patternWith({ 40 }));
}

TEST_CASE("A new edit clears redo and failed edits record nothing", "[undo]") {
    UndoRig rig;
    rig.session.setTempo(100.0);
    rig.later();
    REQUIRE(rig.session.undo());
    REQUIRE(rig.session.canRedo());
    rig.session.setSwing(0.6f);
    CHECK_FALSE(rig.session.canRedo());

    rig.later();
    CHECK_FALSE(rig.session.setParameter(1, "noSuchParameter", 1.0f));
    CHECK_FALSE(rig.session.setPattern(99, 0, {}));
    REQUIRE(rig.session.undo()); // the swing change
    CHECK_FALSE(rig.session.canUndo());
}

TEST_CASE("Undo and redo adding a machine without stopping playback", "[undo]") {
    UndoRig rig;
    rig.session.setPlaying(true);
    rig.render(4800);
    const int added = rig.session.addMachine(SubtractiveSynth::typeName, "Lead");
    REQUIRE(added == 3);
    rig.later();
    rig.session.setParameter(added, "cutoff", 300.0f);
    rig.later();
    rig.session.setPattern(added, 0, patternWith({ 72 }));
    const auto withLead = songText(rig.session);

    REQUIRE(rig.session.undo());
    REQUIRE(rig.session.undo());
    REQUIRE(rig.session.undo());
    CHECK(rig.session.getNumMachines() == 3);
    rig.render(4800);
    CHECK(rig.engine.getNumMachines() == 3);
    CHECK(rig.session.isPlaying());

    REQUIRE(rig.session.redo());
    REQUIRE(rig.session.redo());
    REQUIRE(rig.session.redo());
    rig.render(4800);
    CHECK(rig.engine.getNumMachines() == 4);
    CHECK(rig.session.isPlaying());
    CHECK(songText(rig.session) == withLead);
}

TEST_CASE("Undo removing a machine from the middle brings it back in place", "[undo]") {
    UndoRig rig;
    const auto original = songText(rig.session);
    rig.session.setPlaying(true);
    rig.render(4800);
    REQUIRE(rig.session.removeMachine(1));
    REQUIRE(rig.session.getSong().machines[1].type != AcidBass::typeName);

    REQUIRE(rig.session.undo());
    rig.render(4800);
    CHECK(rig.session.getSong().machines[1].type == AcidBass::typeName);
    CHECK(rig.engine.getNumMachines() == 3);
    CHECK(rig.session.isPlaying()); // the reload restarts playback
    CHECK(songText(rig.session) == original);
}

TEST_CASE("Undoing everything restores the song exactly and redo replays it", "[undo]") {
    UndoRig rig;
    const auto original = songText(rig.session);

    rig.session.setTempo(97.0);
    rig.later();
    rig.session.setMasterParameter("volume", 0.3f);
    rig.later();
    rig.session.setMixerParameter(0, "pan", -0.5f);
    rig.later();
    rig.session.setEffect(2, 0, "Distortion");
    rig.later();
    rig.session.setEffectParameter(2, 0, "drive", 30.0f);
    rig.later();
    rig.session.setEffect(0, 1, "Chorus");
    rig.later();
    rig.session.renameMachine(2, "Strings");
    rig.later();
    rig.session.setBlocks(1, { { 0, 2, 3 } });
    rig.later();
    rig.session.setLoop(true, 2, 6);
    rig.later();
    AutomationLane lane;
    lane.target = { AutomationTarget::Scope::master, -1, -1, "volume" };
    lane.points = { { 0.0, 0.2f }, { 8.0, 0.9f } };
    rig.session.setSongAutomation({ lane });
    rig.later();
    rig.session.setPattern(0, 3, patternWith({ 36, 38 }));
    rig.later();
    rig.session.addMachine(AcidBass::typeName);
    const auto edited = songText(rig.session);

    int steps = 0;
    while (rig.session.undo()) {
        steps++;
    }
    CHECK(steps == 12);
    CHECK(songText(rig.session) == original);
    while (rig.session.redo()) {
    }
    CHECK(songText(rig.session) == edited);
}

TEST_CASE("Loading a song clears the history and history is capped", "[undo]") {
    UndoRig rig;
    for (int i = 0; i < Session::maxUndoSteps + 20; i++) {
        rig.session.setTempo(80.0 + i);
        rig.later();
    }
    int steps = 0;
    while (rig.session.undo()) {
        steps++;
    }
    CHECK(steps == Session::maxUndoSteps);

    rig.session.setTempo(150.0);
    REQUIRE(rig.session.loadSong(makeDemoSong()).wasOk());
    CHECK_FALSE(rig.session.canUndo());
    CHECK_FALSE(rig.session.canRedo());
}
