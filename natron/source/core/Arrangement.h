#pragma once

#include <vector>

// One block on the song timeline: a machine plays `pattern` (0-63) from `startBar` for `lengthBars`,
// looping the pattern inside the block from the block's start.
struct SongBlock {
    int startBar = 0;
    int lengthBars = 1;
    int pattern = 0;

    int endBar() const { return startBar + lengthBars; }
    bool contains(int bar) const { return bar >= startBar && bar < endBar(); }
    bool operator==(const SongBlock& other) const {
        return startBar == other.startBar && lengthBars == other.lengthBars && pattern == other.pattern;
    }
};

// A machine's row on the timeline: sorted by start bar, never overlapping.
using BlockList = std::vector<SongBlock>;

// Editing operations that keep a BlockList valid. Pure functions, used by the song view and tests.
namespace Arrangement {
    constexpr int maxBars = 999;

    // Index of the block covering `bar`, or -1.
    int indexAt(const BlockList& blocks, int bar);

    // Puts `block` on the row. Whatever it overlaps is cut back: a block it covers entirely is
    // removed, one it covers partly is trimmed (or split around it). Returns the new block's index,
    // or -1 if the block is invalid.
    int place(BlockList& blocks, SongBlock block);

    bool remove(BlockList& blocks, int index);

    // Places a copy of block `index` right after it (cutting into whatever is there). Returns the
    // copy's index, or -1 (no such block, or it would run past maxBars).
    int duplicate(BlockList& blocks, int index);

    // Moves / resizes block `index` (then re-places it, so it can cut into neighbours). Return the
    // block's new index, or -1.
    int move(BlockList& blocks, int index, int newStartBar);
    int resize(BlockList& blocks, int index, int newLengthBars);

    // The bar after the last block (0 for an empty row).
    int endBar(const BlockList& blocks);

    // Rebuilds a valid row from arbitrary blocks (later ones win where they overlap).
    BlockList normalised(const BlockList& blocks);

    bool isValid(const BlockList& blocks);
}
