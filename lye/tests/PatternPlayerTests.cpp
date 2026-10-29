// Pattern playback timing. 48 kHz and 120 BPM unless stated: a beat is 24000 samples, a 1/16
// step 6000, a bar 96000.

#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace {
    constexpr int64_t step = 6000;
    constexpr int64_t beat = 24000;
    constexpr int64_t bar = 96000;

    Pattern pattern(std::vector<Note> notes, int bars = 1) {
        Pattern p;
        p.bars = bars;
        p.notes = std::move(notes);
        return p;
    }
}

TEST_CASE("Notes start and end on their beat positions", "[player][timing]") {
    PlayerRenderer r;
    r.render(pattern({ { 0.0, 0.25, 60 }, { 1.0, 0.5, 62 }, { 2.75, 0.25, 64 } }), bar);

    CHECK(r.events == std::vector<RecordedEvent> {
        on(0, 60), off(step, 60),
        on(beat, 62), off(beat + 2 * step, 62),
        on(2 * beat + 3 * step, 64), off(3 * beat, 64),
    });
}

TEST_CASE("Notes longer than a step hold for their full length", "[player][timing]") {
    PlayerRenderer r;
    r.render(pattern({ { 0.0, 3.0, 48 }, { 0.5, 0.25, 72 } }), bar);

    CHECK(r.events == std::vector<RecordedEvent> {
        on(0, 48), on(2 * step, 72), off(3 * step, 72), off(3 * beat, 48),
    });
}

TEST_CASE("Chords start and stop together", "[player][polyphony]") {
    PlayerRenderer r;
    r.render(pattern({ { 0.0, 1.0, 60 }, { 0.0, 1.0, 64 }, { 0.0, 1.0, 67 } }), beat + 1);

    REQUIRE(r.events.size() == 6);
    for (int i = 0; i < 3; i++) {
        CHECK(r.events[static_cast<size_t>(i)].isNoteOn);
        CHECK(r.events[static_cast<size_t>(i)].sample == 0);
        CHECK_FALSE(r.events[static_cast<size_t>(i + 3)].isNoteOn);
        CHECK(r.events[static_cast<size_t>(i + 3)].sample == beat);
    }
}

TEST_CASE("Patterns loop at their length in bars", "[player][timing]") {
    const int bars = GENERATE(1, 2, 4, 8);
    CAPTURE(bars);

    PlayerRenderer r;
    r.render(pattern({ { 0.0, 0.25, 60 } }, bars), 2 * bars * bar + 1);

    CHECK(r.noteOnSamples() == std::vector<int64_t> { 0, bars * bar, 2 * bars * bar });
}

TEST_CASE("A note running past the loop end still gets its note-off", "[player][timing]") {
    PlayerRenderer r;
    r.render(pattern({ { 3.5, 1.0, 50 } }), 2 * bar);

    CHECK(r.events == std::vector<RecordedEvent> {
        on(3 * beat + 2 * step, 50), off(bar + 2 * step, 50),
        on(bar + 3 * beat + 2 * step, 50),
    });
}

TEST_CASE("Retriggering a sounding pitch cuts it first", "[player][polyphony]") {
    PlayerRenderer r;
    r.render(pattern({ { 0.0, 2.0, 60 }, { 1.0, 0.25, 60 } }), bar);

    CHECK(r.events == std::vector<RecordedEvent> {
        on(0, 60), off(beat, 60), on(beat, 60), off(beat + step, 60),
    });
}

TEST_CASE("Back-to-back notes: off comes before on", "[player][timing]") {
    PlayerRenderer r;
    r.render(pattern({ { 0.0, 0.25, 60 }, { 0.25, 0.25, 60 } }), 2 * step + 1);

    CHECK(r.events == std::vector<RecordedEvent> { on(0, 60), off(step, 60), on(step, 60), off(2 * step, 60) });
}

TEST_CASE("Back-to-back different pitches: off comes before on", "[player][timing]") {
    // Matters for mono machines (BassLine): an on before the previous off would read as legato.
    PlayerRenderer r;
    r.render(pattern({ { 0.0, 0.25, 60 }, { 0.25, 0.25, 62 } }), 2 * step + 1);

    CHECK(r.events == std::vector<RecordedEvent> { on(0, 60), off(step, 60), on(step, 62), off(2 * step, 62) });
}

TEST_CASE("Event positions do not depend on the host block size", "[player][timing]") {
    const int blockSize = GENERATE(1, 7, 64, 441, 512, 4096);
    CAPTURE(blockSize);

    const auto p = pattern({ { 0.0, 0.25, 60 }, { 0.75, 2.0, 62 }, { 1.25, 0.1, 64 }, { 3.0, 1.5, 65 },
                             { 3.0, 0.5, 69 } }, 1);

    PlayerRenderer reference;
    reference.render(p, 3 * bar, 4096);

    PlayerRenderer r;
    r.render(p, 3 * bar, blockSize);

    CHECK(r.events == reference.events);
}

TEST_CASE("Odd tempos and sample rates stay block-size independent", "[player][timing]") {
    const int blockSize = GENERATE(1, 13, 480, 1024);
    CAPTURE(blockSize);

    const auto p = pattern({ { 0.0, 0.25, 60 }, { 0.333, 0.5, 62 }, { 2.1, 1.7, 64 } }, 1);

    PlayerRenderer reference(44100.0, 133.7);
    reference.render(p, 400000, 1);

    PlayerRenderer r(44100.0, 133.7);
    r.render(p, 400000, blockSize);

    CHECK(r.events == reference.events);
}

TEST_CASE("A tempo change mid-note stretches the rest of the note", "[player][timing]") {
    PlayerRenderer r;
    const auto p = pattern({ { 0.0, 2.0, 60 } });

    r.render(p, beat);   // first beat at 120 BPM
    r.setTempo(60.0);    // second beat takes 48000 samples
    r.render(p, 2 * beat + 1);

    CHECK(r.events == std::vector<RecordedEvent> { on(0, 60), off(beat + 2 * beat, 60) });
}

TEST_CASE("releaseAll ends every sounding note", "[player][transport]") {
    PlayerRenderer r;
    r.render(pattern({ { 0.0, 4.0, 60 }, { 0.0, 4.0, 64 } }), step);
    REQUIRE(r.player.getNumSoundingNotes() == 2);

    r.releaseAll();

    CHECK(r.player.getNumSoundingNotes() == 0);
    CHECK(r.events.size() == 4);
}

TEST_CASE("Notes outside the pattern length are ignored", "[player]") {
    PlayerRenderer r;
    r.render(pattern({ { 4.0, 0.25, 60 }, { -1.0, 0.25, 62 } }, 1), 2 * bar);

    CHECK(r.events.empty());
}
