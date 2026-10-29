#include "Arrangement.h"

#include <algorithm>

namespace {
    bool isValidBlock(const SongBlock& block) {
        return block.startBar >= 0 && block.lengthBars >= 1 && block.endBar() <= Arrangement::maxBars
               && block.pattern >= 0 && block.pattern < 64;
    }
}

int Arrangement::indexAt(const BlockList& blocks, int bar) {
    for (size_t i = 0; i < blocks.size(); i++) {
        if (blocks[i].contains(bar)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int Arrangement::place(BlockList& blocks, SongBlock block) {
    if (!isValidBlock(block)) {
        return -1;
    }

    BlockList result;
    for (const auto& existing : blocks) {
        if (existing.endBar() <= block.startBar || existing.startBar >= block.endBar()) {
            result.push_back(existing); // no overlap
            continue;
        }
        if (existing.startBar < block.startBar) { // keep the part before the new block
            result.push_back({ existing.startBar, block.startBar - existing.startBar, existing.pattern });
        }
        if (existing.endBar() > block.endBar()) { // and the part after it
            result.push_back({ block.endBar(), existing.endBar() - block.endBar(), existing.pattern });
        }
    }
    result.push_back(block);
    std::sort(result.begin(), result.end(),
              [](const SongBlock& a, const SongBlock& b) { return a.startBar < b.startBar; });
    blocks = std::move(result);
    return indexAt(blocks, block.startBar);
}

bool Arrangement::remove(BlockList& blocks, int index) {
    if (index < 0 || index >= static_cast<int>(blocks.size())) {
        return false;
    }
    blocks.erase(blocks.begin() + index);
    return true;
}

int Arrangement::move(BlockList& blocks, int index, int newStartBar) {
    if (index < 0 || index >= static_cast<int>(blocks.size())) {
        return -1;
    }
    SongBlock block = blocks[static_cast<size_t>(index)];
    block.startBar = newStartBar;
    if (!isValidBlock(block)) {
        return -1;
    }
    remove(blocks, index);
    return place(blocks, block);
}

int Arrangement::resize(BlockList& blocks, int index, int newLengthBars) {
    if (index < 0 || index >= static_cast<int>(blocks.size())) {
        return -1;
    }
    SongBlock block = blocks[static_cast<size_t>(index)];
    block.lengthBars = newLengthBars;
    if (!isValidBlock(block)) {
        return -1;
    }
    remove(blocks, index);
    return place(blocks, block);
}

int Arrangement::endBar(const BlockList& blocks) {
    int end = 0;
    for (const auto& block : blocks) {
        end = std::max(end, block.endBar());
    }
    return end;
}

BlockList Arrangement::normalised(const BlockList& blocks) {
    BlockList result;
    for (const auto& block : blocks) {
        place(result, block);
    }
    return result;
}

bool Arrangement::isValid(const BlockList& blocks) {
    for (size_t i = 0; i < blocks.size(); i++) {
        if (!isValidBlock(blocks[i])) {
            return false;
        }
        if (i > 0 && blocks[i - 1].endBar() > blocks[i].startBar) {
            return false;
        }
    }
    return true;
}
