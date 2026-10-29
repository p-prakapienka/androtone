// Song-mode playback, observed through a RecordingMachine. 48 kHz, 120 BPM: bar = 96000 samples.

#include "TestHelpers.h"
#include "TestMachines.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace {
    constexpr int64_t bar = 96000;

    Pattern onePitch(int pitch, int bars = 1, double length = 0.25) {
        Pattern p;
        p.bars = bars;
        p.notes.push_back({ 0.0, length, pitch, 0.8f });
        return p;
    }

    struct SongRig : Rig {
        SongRig() {
            RecordingMachine::registerType();
            machine = session.addMachine(RecordingMachine::typeName);
            session.setPattern(machine, 0, onePitch(60));
            session.setPattern(machine, 1, onePitch(62));
            Pattern twoBars = onePitch(64, 2);
            twoBars.notes.push_back({ 4.0, 0.25, 65, 0.8f });
            session.setPattern(machine, 2, twoBars);
            render(1);
            recorder = RecordingMachine::instances().back();
            recorder->events.clear();
            recorder->position = 0;
        }

        std::vector<std::pair<int64_t, int>> ons() const {
            std::vector<std::pair<int64_t, int>> result;
            for (const auto& e : recorder->noteOns()) {
                result.emplace_back(e.sample, e.pitch);
            }
            return result;
        }

        int machine = -1;
        RecordingMachine* recorder = nullptr;
    };

    using Ons = std::vector<std::pair<int64_t, int>>;
}

TEST_CASE("Song mode plays each block's pattern on its bars", "[song]") {
    SongRig r;
    r.session.setBlocks(r.machine, { { 0, 2, 0 }, { 2, 1, 1 } });
    r.session.setSongMode(true);
    r.session.setPlaying(true);
    r.render(3 * bar - 1);

    CHECK(r.ons() == Ons { { 0, 60 }, { bar, 60 }, { 2 * bar, 62 } });
}

TEST_CASE("A pattern loops from its block's start", "[song]") {
    SongRig r;
    r.session.setBlocks(r.machine, { { 1, 4, 2 } }); // a 2-bar pattern in a 4-bar block from bar 2
    r.session.setSongMode(true);
    r.session.setPlaying(true);
    r.render(5 * bar - 1);

    CHECK(r.ons() == Ons { { bar, 64 }, { 2 * bar, 65 }, { 3 * bar, 64 }, { 4 * bar, 65 } });
}

TEST_CASE("Gaps between blocks are silent", "[song]") {
    SongRig r;
    r.session.setBlocks(r.machine, { { 0, 1, 0 }, { 3, 1, 1 } });
    r.session.setSongMode(true);
    r.session.setPlaying(true);
    r.render(4 * bar - 1);

    CHECK(r.ons() == Ons { { 0, 60 }, { 3 * bar, 62 } });
}

TEST_CASE("The song stops at its end and releases held notes", "[song]") {
    SongRig r;
    r.session.setPattern(r.machine, 3, onePitch(70, 1, 8.0)); // rings past the block
    r.session.setBlocks(r.machine, { { 0, 1, 3 } });
    r.session.setSongMode(true);
    r.session.setPlaying(true);
    r.render(bar + 1000);

    CHECK_FALSE(r.engine.isPlaying());
    bool released = false;
    for (const auto& e : r.recorder->events) {
        released = released || (!e.isNoteOn && e.pitch == 70);
    }
    CHECK(released);
}

TEST_CASE("The loop region repeats", "[song]") {
    SongRig r;
    r.session.setBlocks(r.machine, { { 0, 1, 0 }, { 1, 1, 1 }, { 2, 1, 0 } });
    r.session.setLoop(true, 1, 3);
    r.session.setSongMode(true);
    r.session.setPlaying(true);
    r.render(4 * bar - 1);

    // Starts at the loop start (bar 2), plays bars 2-3, then jumps back to bar 2.
    CHECK(r.ons() == Ons { { 0, 62 }, { bar, 60 }, { 2 * bar, 62 }, { 3 * bar, 60 } });
    CHECK(r.engine.isPlaying());
}

TEST_CASE("A note held at the loop end is released at the jump", "[song]") {
    SongRig r;
    Pattern late;
    late.notes.push_back({ 2.0, 6.0, 70, 0.8f }); // starts mid-bar, still held at the loop end
    r.session.setPattern(r.machine, 3, late);
    r.session.setBlocks(r.machine, { { 0, 1, 3 } });
    r.session.setLoop(true, 0, 1);
    r.session.setSongMode(true);
    r.session.setPlaying(true);
    r.render(bar + 100);

    // Released exactly at the jump, not when the note would have ended (or when it restarts).
    int64_t offAt = -1;
    for (const auto& e : r.recorder->events) {
        if (!e.isNoteOn && e.pitch == 70 && offAt < 0) {
            offAt = e.sample;
        }
    }
    CHECK(offAt == bar);
    CHECK(r.ons() == Ons { { bar / 2, 70 } });
}

TEST_CASE("Song-mode timing does not depend on the block size", "[song]") {
    const int blockSize = GENERATE(1, 37, 512, 4096);
    CAPTURE(blockSize);

    auto run = [](int hostBlock) {
        SongRig r;
        r.session.setBlocks(r.machine, { { 0, 1, 0 }, { 1, 2, 2 }, { 3, 1, 1 } });
        r.session.setLoop(true, 0, 4);
        r.session.setSongMode(true);
        r.session.setPlaying(true);
        r.render(9 * bar, hostBlock);
        return r.recorder->events;
    };
    CHECK(run(blockSize) == run(512));
}

TEST_CASE("Song mode leaves the pattern selection alone", "[song]") {
    SongRig r;
    r.session.selectPattern(r.machine, 1);
    r.session.setBlocks(r.machine, { { 0, 1, 0 } });
    r.session.setSongMode(true);
    r.session.setPlaying(true);
    r.render(bar / 2);
    CHECK(r.engine.getCurrentPattern(r.machine) == 0); // shows the block's pattern while playing
    r.session.setPlaying(false);
    r.render(512);

    CHECK(r.engine.getCurrentPattern(r.machine) == 1);
    r.session.setSongMode(false);
    r.recorder->events.clear();
    r.session.setPlaying(true);
    r.render(bar / 2);
    CHECK(r.ons() == Ons { { r.recorder->noteOns().front().sample, 62 } });
}

TEST_CASE("Switching to pattern mode while playing continues from the next bar", "[song]") {
    SongRig r;
    r.session.setBlocks(r.machine, { { 0, 8, 1 } });
    r.session.selectPattern(r.machine, 0);
    r.session.setSongMode(true);
    r.session.setPlaying(true);
    r.render(bar + bar / 2);

    r.session.setSongMode(false);
    r.render(bar);

    const auto ons = r.ons();
    REQUIRE(ons.size() == 3);
    CHECK(ons[2] == std::pair<int64_t, int> { 2 * bar, 60 }); // pattern A01 from the bar line
}
