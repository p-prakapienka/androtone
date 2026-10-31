#include "core/Arrangement.h"
#include "core/PatternEditing.h"

#include <catch2/catch_test_macros.hpp>

// ---------------------------------------------------------------------------- Arrangement

TEST_CASE("Placing blocks keeps the row sorted and non-overlapping", "[arrangement]") {
    BlockList row;
    CHECK(Arrangement::place(row, { 8, 4, 1 }) == 0);
    CHECK(Arrangement::place(row, { 0, 4, 0 }) == 0);
    CHECK(row == BlockList { { 0, 4, 0 }, { 8, 4, 1 } });
    CHECK(Arrangement::isValid(row));
    CHECK(Arrangement::endBar(row) == 12);
}

TEST_CASE("A new block trims, splits, or replaces what it overlaps", "[arrangement]") {
    SECTION("covers entirely") {
        BlockList row { { 2, 2, 0 } };
        Arrangement::place(row, { 0, 8, 5 });
        CHECK(row == BlockList { { 0, 8, 5 } });
    }
    SECTION("overlaps the end of one") {
        BlockList row { { 0, 4, 0 } };
        Arrangement::place(row, { 2, 4, 5 });
        CHECK(row == BlockList { { 0, 2, 0 }, { 2, 4, 5 } });
    }
    SECTION("overlaps the start of one") {
        BlockList row { { 4, 4, 0 } };
        Arrangement::place(row, { 2, 4, 5 });
        CHECK(row == BlockList { { 2, 4, 5 }, { 6, 2, 0 } });
    }
    SECTION("lands inside one: splits it") {
        BlockList row { { 0, 8, 0 } };
        Arrangement::place(row, { 3, 2, 5 });
        CHECK(row == BlockList { { 0, 3, 0 }, { 3, 2, 5 }, { 5, 3, 0 } });
    }
}

TEST_CASE("Invalid blocks are refused", "[arrangement]") {
    BlockList row;
    CHECK(Arrangement::place(row, { -1, 4, 0 }) == -1);
    CHECK(Arrangement::place(row, { 0, 0, 0 }) == -1);
    CHECK(Arrangement::place(row, { 0, 1, 64 }) == -1);
    CHECK(Arrangement::place(row, { Arrangement::maxBars, 1, 0 }) == -1);
    CHECK(row.empty());
    CHECK_FALSE(Arrangement::isValid({ { 0, 4, 0 }, { 2, 4, 0 } }));
}

TEST_CASE("Blocks can be found, moved, resized, and removed", "[arrangement]") {
    BlockList row { { 0, 4, 0 }, { 4, 4, 1 } };
    CHECK(Arrangement::indexAt(row, 5) == 1);
    CHECK(Arrangement::indexAt(row, 8) == -1);

    const int moved = Arrangement::move(row, 1, 10);
    CHECK(row == BlockList { { 0, 4, 0 }, { 10, 4, 1 } });
    CHECK(moved == 1);

    CHECK(Arrangement::resize(row, 0, 12) == 0); // grows into the moved block and trims it
    CHECK(row == BlockList { { 0, 12, 0 }, { 12, 2, 1 } });

    CHECK(Arrangement::remove(row, 0));
    CHECK(row == BlockList { { 12, 2, 1 } });
    CHECK_FALSE(Arrangement::remove(row, 3));
    CHECK(Arrangement::resize(row, 0, 0) == -1);
}

TEST_CASE("Normalising resolves overlaps in order", "[arrangement]") {
    const auto row = Arrangement::normalised({ { 0, 8, 0 }, { 4, 2, 1 }, { 20, 1, 2 } });
    CHECK(row == BlockList { { 0, 4, 0 }, { 4, 2, 1 }, { 6, 2, 0 }, { 20, 1, 2 } });
}

// ---------------------------------------------------------------------------- Pattern editing

TEST_CASE("Tapping the grid adds and removes notes", "[editing]") {
    Pattern p;
    CHECK(PatternEditing::toggle(p, 1.1, 60)); // snaps to the 1/16 grid
    REQUIRE(p.notes.size() == 1);
    CHECK(juce::exactlyEqual(p.notes[0].start, 1.0));
    CHECK(juce::exactlyEqual(p.notes[0].length, Pattern::stepLength));

    CHECK_FALSE(PatternEditing::toggle(p, 1.2, 60)); // inside the note: removes it
    CHECK(p.notes.empty());
}

TEST_CASE("Adding a note where one starts replaces it", "[editing]") {
    Pattern p;
    PatternEditing::add(p, 0.5, 60, 1.0, 0.5f);
    PatternEditing::add(p, 0.5, 60, 0.25, 0.9f);
    REQUIRE(p.notes.size() == 1);
    CHECK(juce::exactlyEqual(p.notes[0].velocity, 0.9f));
    CHECK(PatternEditing::add(p, 4.0, 60) == -1); // outside a 1-bar pattern
    CHECK(PatternEditing::add(p, 0.0, 128) == -1);
}

TEST_CASE("Notes stay sorted, and chords are allowed", "[editing]") {
    Pattern p;
    PatternEditing::add(p, 2.0, 60);
    PatternEditing::add(p, 0.0, 67);
    PatternEditing::add(p, 0.0, 64);
    REQUIRE(p.notes.size() == 3);
    CHECK(p.notes[0].pitch == 64);
    CHECK(p.notes[1].pitch == 67);
    CHECK(p.notes[2].pitch == 60);
}

TEST_CASE("Moving and resizing stay inside the pattern", "[editing]") {
    Pattern p;
    const int index = PatternEditing::add(p, 0.0, 60);
    const int moved = PatternEditing::move(p, index, 10.0, 200);
    REQUIRE(moved >= 0);
    CHECK(p.notes[static_cast<size_t>(moved)].start < p.lengthInBeats());
    CHECK(p.notes[static_cast<size_t>(moved)].pitch == 127);

    CHECK(PatternEditing::resize(p, moved, 0.0));
    CHECK(juce::exactlyEqual(p.notes[static_cast<size_t>(moved)].length, PatternEditing::minimumLength));
    CHECK(PatternEditing::resize(p, moved, 99.0));
    CHECK(juce::exactlyEqual(p.notes[static_cast<size_t>(moved)].length, p.lengthInBeats()));
}

TEST_CASE("Velocity cycles default, accent, soft", "[editing]") {
    Pattern p;
    const int i = PatternEditing::add(p, 0.0, 60);
    auto velocity = [&] { return p.notes[static_cast<size_t>(i)].velocity; };
    CHECK(juce::exactlyEqual(velocity(), PatternEditing::defaultVelocity));
    PatternEditing::cycleVelocity(p, i);
    CHECK(juce::exactlyEqual(velocity(), PatternEditing::accentVelocity));
    PatternEditing::cycleVelocity(p, i);
    CHECK(juce::exactlyEqual(velocity(), PatternEditing::softVelocity));
    PatternEditing::cycleVelocity(p, i);
    CHECK(juce::exactlyEqual(velocity(), PatternEditing::defaultVelocity));
}

TEST_CASE("Pattern length changes keep notes", "[editing]") {
    Pattern p;
    p.bars = 2;
    PatternEditing::add(p, 6.0, 60);
    CHECK(PatternEditing::setBars(p, 1));
    CHECK(p.notes.size() == 1); // kept, just not played
    CHECK_FALSE(PatternEditing::setBars(p, 3));
    CHECK(p.bars == 1);
}

TEST_CASE("Duplicating a block puts a copy right after it", "[arrangement]") {
    BlockList row { { 0, 2, 5 }, { 3, 2, 7 } };
    CHECK(Arrangement::duplicate(row, 0) == 1);
    REQUIRE(row.size() == 3);
    CHECK(row[1] == SongBlock { 2, 2, 5 });
    CHECK(row[2] == SongBlock { 4, 1, 7 }); // the copy cut into the next block
    CHECK(Arrangement::isValid(row));

    BlockList end { { Arrangement::maxBars - 1, 1, 0 } };
    CHECK(Arrangement::duplicate(end, 0) == -1);
    CHECK(Arrangement::duplicate(end, 3) == -1);
    CHECK(end.size() == 1);
}

TEST_CASE("Transposing moves every note or none", "[editing]") {
    Pattern p;
    p.notes = { { 0.0, 0.25, 60, 0.8f }, { 1.0, 0.25, 64, 0.8f } };
    CHECK(PatternEditing::transpose(p, 12));
    CHECK(p.notes[0].pitch == 72);
    CHECK(p.notes[1].pitch == 76);
    CHECK_FALSE(PatternEditing::transpose(p, 60)); // 76 + 60 > 127
    CHECK(p.notes[1].pitch == 76);
    CHECK(PatternEditing::transpose(p, -72));
    CHECK(p.notes[0].pitch == 0);
}

TEST_CASE("Doubling a pattern repeats its notes and automation", "[editing]") {
    Pattern p;
    p.bars = 2;
    p.notes = { { 0.0, 0.5, 60, 0.8f }, { 7.5, 1.0, 62, 1.0f }, { 9.0, 0.25, 70, 0.8f } }; // the last is past the end
    AutomationLane lane;
    lane.target.parameterId = "cutoff";
    lane.points = { { 0.0, 0.1f }, { 4.0, 0.9f }, { 8.0, 0.3f } };
    p.automation = { lane };

    REQUIRE(PatternEditing::doubleLength(p));
    CHECK(p.bars == 4);
    REQUIRE(p.notes.size() == 4);
    CHECK(p.notes[0] == Note { 0.0, 0.5, 60, 0.8f });
    CHECK(p.notes[1] == Note { 7.5, 1.0, 62, 1.0f });
    CHECK(p.notes[2] == Note { 8.0, 0.5, 60, 0.8f });
    CHECK(p.notes[3] == Note { 15.5, 1.0, 62, 1.0f });
    const std::vector<AutomationPoint> expected { { 0.0, 0.1f }, { 4.0, 0.9f }, { 8.0, 0.1f }, { 12.0, 0.9f }, { 16.0, 0.3f } };
    CHECK(p.automation[0].points == expected);

    p.bars = 8;
    CHECK_FALSE(PatternEditing::doubleLength(p));
}

TEST_CASE("Clearing and pasting patterns", "[editing]") {
    Pattern p;
    p.bars = 2;
    p.notes = { { 0.0, 0.25, 60, 0.8f } };
    AutomationLane lane;
    lane.target.parameterId = "cutoff";
    lane.points = { { 0.0, 0.5f } };
    p.automation = { lane };

    CHECK(PatternEditing::forPaste(p, true) == p);
    const auto other = PatternEditing::forPaste(p, false);
    CHECK(other.notes == p.notes);
    CHECK(other.automation.empty());
    CHECK(other.bars == 2);

    PatternEditing::clear(p);
    CHECK(p.isEmpty());
    CHECK(p.bars == 2);
}
