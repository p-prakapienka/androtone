// Pattern banks and pattern switching, observed through a RecordingMachine.
// 48 kHz, 120 BPM: beat = 24000 samples, bar = 96000.

#include "TestHelpers.h"
#include "TestMachines.h"

#include <catch2/catch_test_macros.hpp>

namespace {
    constexpr int64_t beat = 24000;
    constexpr int64_t bar = 96000;

    Pattern onePitch(int pitch, int bars = 1, double length = 0.25) {
        Pattern p;
        p.bars = bars;
        p.notes.push_back({ 0.0, length, pitch, 0.8f });
        return p;
    }

    struct BankRig : Rig {
        BankRig() {
            RecordingMachine::registerType();
            machine = session.addMachine(RecordingMachine::typeName);
            render(1); // apply, so the recorder exists
            recorder = RecordingMachine::instances().back();
            recorder->events.clear();
            recorder->position = 0;
        }

        std::vector<int> pitches() const {
            std::vector<int> result;
            for (const auto& e : recorder->noteOns()) {
                result.push_back(e.pitch);
            }
            return result;
        }

        std::vector<int64_t> onSamples() const {
            std::vector<int64_t> result;
            for (const auto& e : recorder->noteOns()) {
                result.push_back(e.sample);
            }
            return result;
        }

        int machine = -1;
        RecordingMachine* recorder = nullptr;
    };
}

TEST_CASE("Pattern names run A01 to D16", "[patterns]") {
    CHECK(PatternBank::nameOf(0) == "A01");
    CHECK(PatternBank::nameOf(15) == "A16");
    CHECK(PatternBank::nameOf(16) == "B01");
    CHECK(PatternBank::nameOf(63) == "D16");
    CHECK(PatternBank::indexOf(2, 4) == 36);
    CHECK(PatternBank::bankOf(36) == 2);
    CHECK(PatternBank::slotOf(36) == 4);
}

TEST_CASE("Selecting a pattern while stopped takes effect immediately", "[patterns]") {
    BankRig r;
    r.session.setPattern(r.machine, 0, onePitch(60));
    r.session.setPattern(r.machine, 17, onePitch(72));
    r.session.selectPattern(r.machine, 17);
    r.render(512);
    CHECK(r.engine.getCurrentPattern(r.machine) == 17);
    CHECK(r.engine.getQueuedPattern(r.machine) == -1);

    r.session.setPlaying(true);
    r.render(bar);
    CHECK(r.pitches() == std::vector<int> { 72 });
}

TEST_CASE("Selecting while playing waits for the end of the current pattern", "[patterns]") {
    BankRig r;
    r.session.setPattern(r.machine, 0, onePitch(60));
    r.session.setPattern(r.machine, 1, onePitch(62));
    r.session.setPlaying(true);
    r.render(beat + 100); // into the first bar

    r.session.selectPattern(r.machine, 1);
    r.render(512);
    CHECK(r.engine.getCurrentPattern(r.machine) == 0);
    CHECK(r.engine.getQueuedPattern(r.machine) == 1);

    r.render(2 * bar);
    CHECK(r.engine.getCurrentPattern(r.machine) == 1);
    CHECK(r.engine.getQueuedPattern(r.machine) == -1);
    CHECK(r.pitches() == std::vector<int> { 60, 62, 62 });
    CHECK(r.onSamples() == std::vector<int64_t> { 0, bar, 2 * bar });
}

TEST_CASE("A longer pattern finishes before switching", "[patterns]") {
    BankRig r;
    Pattern twoBars = onePitch(60, 2);
    twoBars.notes.push_back({ 4.0, 0.25, 61, 0.8f });
    r.session.setPattern(r.machine, 0, twoBars);
    r.session.setPattern(r.machine, 1, onePitch(62));
    r.session.setPlaying(true);
    r.render(beat);

    r.session.selectPattern(r.machine, 1);
    r.render(3 * bar - beat); // up to the end of bar 3

    CHECK(r.pitches() == std::vector<int> { 60, 61, 62 });
    CHECK(r.onSamples() == std::vector<int64_t> { 0, bar, 2 * bar });
}

TEST_CASE("The new pattern loops from where it started", "[patterns]") {
    BankRig r;
    r.session.setPattern(r.machine, 0, onePitch(60, 1));
    Pattern twoBars = onePitch(62, 2);
    twoBars.notes.push_back({ 4.0, 0.25, 63, 0.8f });
    r.session.setPattern(r.machine, 1, twoBars);
    r.session.setPlaying(true);
    r.render(100);

    r.session.selectPattern(r.machine, 1);
    r.render(5 * bar - 100); // up to the end of bar 5

    // A01 for bar 1, then A02 (2 bars) from bar 2: 62 at bar 2, 63 at bar 3, 62 at bar 4, 63 at bar 5.
    CHECK(r.pitches() == std::vector<int> { 60, 62, 63, 62, 63 });
    CHECK(r.onSamples() == std::vector<int64_t> { 0, bar, 2 * bar, 3 * bar, 4 * bar });
}

TEST_CASE("Selecting the playing pattern again cancels nothing and queues nothing", "[patterns]") {
    BankRig r;
    r.session.setPattern(r.machine, 0, onePitch(60));
    r.session.setPlaying(true);
    r.render(beat);
    r.session.selectPattern(r.machine, 0);
    r.render(512);
    CHECK(r.engine.getQueuedPattern(r.machine) == -1);
}

TEST_CASE("Stopping applies a queued switch at once", "[patterns][transport]") {
    BankRig r;
    r.session.setPattern(r.machine, 0, onePitch(60));
    r.session.setPattern(r.machine, 5, onePitch(65));
    r.session.setPlaying(true);
    r.render(beat);
    r.session.selectPattern(r.machine, 5);
    r.render(512);
    r.session.setPlaying(false);
    r.render(512);

    CHECK(r.engine.getCurrentPattern(r.machine) == 5);
    CHECK(r.engine.getQueuedPattern(r.machine) == -1);

    r.recorder->events.clear();
    r.session.setPlaying(true);
    r.render(beat);
    CHECK(r.pitches() == std::vector<int> { 65 });
}

TEST_CASE("A note held across a pattern switch still ends on time", "[patterns]") {
    BankRig r;
    Pattern longNote = onePitch(48, 1, 6.0); // runs 2 beats into the next pattern
    r.session.setPattern(r.machine, 0, longNote);
    r.session.setPattern(r.machine, 1, onePitch(72));
    r.session.setPlaying(true);
    r.render(beat);
    r.session.selectPattern(r.machine, 1);
    r.render(bar + 3 * beat);

    bool sawOff = false;
    for (const auto& e : r.recorder->events) {
        if (!e.isNoteOn && e.pitch == 48) {
            sawOff = true;
            CHECK(e.sample == bar + 2 * beat);
        }
    }
    CHECK(sawOff);
}

TEST_CASE("An empty pattern still has a length and keeps time", "[patterns]") {
    BankRig r;
    Pattern empty;
    empty.bars = 2;
    r.session.setPattern(r.machine, 0, empty);
    r.session.setPattern(r.machine, 1, onePitch(70));
    r.session.setPlaying(true);
    r.render(beat);
    r.session.selectPattern(r.machine, 1);
    r.render(3 * bar - beat); // up to the end of bar 3

    CHECK(r.onSamples() == std::vector<int64_t> { 2 * bar });
}
