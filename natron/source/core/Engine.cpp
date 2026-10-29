#include "Engine.h"

#include <cmath>

namespace {
    const Pattern emptyPattern {};
}

Engine::Engine() {
    commands.resize(static_cast<size_t>(commandFifo.getTotalSize()));
    garbage.resize(static_cast<size_t>(garbageFifo.getTotalSize()));
    for (size_t i = 0; i < maxMachines; i++) {
        publishedCurrent[i].store(0);
        publishedQueued[i].store(-1);
    }
}

Engine::~Engine() {
    // No audio is running any more: drop unapplied commands, then everything the engine owns.
    commandFifo.read(commandFifo.getNumReady()).forEach([this](int i) {
        deletePayload(commands[static_cast<size_t>(i)]);
    });
    for (auto& slot : slots) {
        for (auto*& pattern : slot.patterns) {
            delete pattern;
            pattern = nullptr;
        }
        delete slot.blocks;
        slot.blocks = nullptr;
    }
    collectGarbage();
}

bool Engine::post(const EngineCommand& command) {
    if (commandFifo.getFreeSpace() < 1) {
        jassertfalse; // the audio thread isn't draining the queue (not running?) or edits are too bursty
        deletePayload(command);
        return false;
    }
    commandFifo.write(1).forEach([&](int i) { commands[static_cast<size_t>(i)] = command; });
    return true;
}

void Engine::collectGarbage() {
    garbageFifo.read(garbageFifo.getNumReady()).forEach([this](int i) {
        auto& item = garbage[static_cast<size_t>(i)];
        item.destroy(item.object);
        item = {};
    });
}

void Engine::deletePayload(const EngineCommand& command) {
    delete command.newMachine;
    delete command.newChannel;
    delete command.newPattern;
    delete command.newEffect;
    delete command.newBlocks;
}

void Engine::prepare(double newSampleRate, int maxBlockSize) {
    sampleRate.store(newSampleRate);
    maxBlock.store(juce::jmax(1, maxBlockSize));
    transport.prepare(newSampleRate);
    transport.setTempo(tempo.load());
    wasPlaying = false;

    // No callback is running, so it's safe to take edits queued before audio started; that way
    // the machines they add are re-prepared below at the device's real rate.
    applyCommands();

    for (auto& slot : slots) {
        slot.buffer.setSize(2, maxBlock.load());
        slot.midi.ensureSize(4096);
        if (slot.machine != nullptr) {
            slot.machine->prepare(newSampleRate, maxBlock.load());
            slot.channel->prepare(newSampleRate);
            for (auto& effect : slot.effects) {
                if (effect != nullptr) {
                    effect->prepare(newSampleRate, maxBlock.load());
                }
            }
        }
    }

    mixBus.setSize(2, maxBlock.load());
    delayBus.setSize(2, maxBlock.load());
    reverbBus.setSize(2, maxBlock.load());
    master.prepare(newSampleRate, maxBlock.load());
}

void Engine::applyCommands() {
    commandFifo.read(commandFifo.getNumReady()).forEach([this](int i) {
        apply(commands[static_cast<size_t>(i)]);
        commands[static_cast<size_t>(i)] = {};
    });
    publishedNumMachines.store(numMachines);
}

void Engine::apply(const EngineCommand& command) {
    const bool validMachine = command.machine >= 0 && command.machine < numMachines;
    const bool validPattern = command.index >= 0 && command.index < PatternBank::numPatterns;

    switch (command.type) {
        case EngineCommand::Type::addMachine:
            if (numMachines < maxMachines && command.newMachine != nullptr && command.newChannel != nullptr) {
                auto& slot = slots[static_cast<size_t>(numMachines)];
                slot.machine.reset(command.newMachine);
                slot.channel.reset(command.newChannel);
                publish(numMachines);
                numMachines++;
            } else {
                trash(command.newMachine);
                trash(command.newChannel);
            }
            break;

        case EngineCommand::Type::removeMachine:
            if (validMachine) {
                removeSlot(command.machine);
            }
            break;

        case EngineCommand::Type::clearRack:
            while (numMachines > 0) {
                removeSlot(numMachines - 1);
            }
            break;

        case EngineCommand::Type::setPattern:
            if (validMachine && validPattern) {
                auto*& target = slots[static_cast<size_t>(command.machine)].patterns[static_cast<size_t>(command.index)];
                trash(target);
                target = command.newPattern;
            } else {
                trash(command.newPattern);
            }
            break;

        case EngineCommand::Type::selectPattern:
            if (validMachine && validPattern) {
                auto& slot = slots[static_cast<size_t>(command.machine)];
                if (wasPlaying && !wasSongMode && command.index != slot.current) {
                    slot.queued = command.index;
                } else if (!wasSongMode || !wasPlaying) {
                    slot.current = command.index;
                    slot.queued = -1;
                }
                publish(command.machine);
            }
            break;

        case EngineCommand::Type::setEffect:
            if (validMachine && command.index >= 0 && command.index < effectsPerMachine) {
                auto& target = slots[static_cast<size_t>(command.machine)].effects[static_cast<size_t>(command.index)];
                trash(target.release());
                target.reset(command.newEffect);
            } else {
                trash(command.newEffect);
            }
            break;

        case EngineCommand::Type::setBlocks:
            if (validMachine) {
                auto*& target = slots[static_cast<size_t>(command.machine)].blocks;
                trash(target);
                target = command.newBlocks;
            } else {
                trash(command.newBlocks);
            }
            break;
    }
}

void Engine::removeSlot(int index) {
    // Bubble the removed slot to the end with swaps (no allocation); channels move with machines.
    for (int i = index; i < numMachines - 1; i++) {
        std::swap(slots[static_cast<size_t>(i)], slots[static_cast<size_t>(i + 1)]);
        publish(i);
    }
    numMachines--;
    clearSlot(slots[static_cast<size_t>(numMachines)]);
    publish(numMachines);
}

void Engine::clearSlot(Slot& slot) {
    trash(slot.machine.release());
    trash(slot.channel.release());
    for (auto& effect : slot.effects) {
        trash(effect.release());
    }
    for (auto*& pattern : slot.patterns) {
        trash(pattern);
        pattern = nullptr;
    }
    trash(slot.blocks);
    slot.blocks = nullptr;
    juce::MidiBuffer discard;
    slot.player.releaseAll(discard, 0);
    slot.current = 0;
    slot.queued = -1;
    slot.originBeat = 0.0;
}

void Engine::publish(int index) {
    const auto& slot = slots[static_cast<size_t>(index)];
    publishedCurrent[static_cast<size_t>(index)].store(slot.current);
    publishedQueued[static_cast<size_t>(index)].store(slot.queued);
}

const Pattern& Engine::patternAt(const Slot& slot, int index) const {
    const auto* pattern = slot.patterns[static_cast<size_t>(index)];
    return pattern != nullptr ? *pattern : emptyPattern;
}

bool Engine::loopIsActive() const {
    return loopEnabled.load() && loopEndBar.load() > loopStartBar.load() && loopStartBar.load() >= 0;
}

int Engine::songEndBar() const {
    int end = 0;
    for (int i = 0; i < numMachines; i++) {
        if (const auto* blocks = slots[static_cast<size_t>(i)].blocks) {
            end = juce::jmax(end, Arrangement::endBar(*blocks));
        }
    }
    return end;
}

void Engine::process(juce::AudioBuffer<float>& output) {
    juce::ScopedNoDenormals noDenormals;
    output.clear();
    applyCommands();

    // Hosts (and Android in particular) may hand over more than they announced; work in chunks
    // no larger than what the machines were prepared for.
    const int chunkSize = maxBlock.load();
    for (int start = 0; start < output.getNumSamples(); start += chunkSize) {
        processChunk(output, start, juce::jmin(chunkSize, output.getNumSamples() - start));
    }
}

void Engine::processChunk(juce::AudioBuffer<float>& output, int startSample, int numSamples) {
    const bool shouldPlay = playing.load();
    const bool songModeNow = songMode.load();
    const bool started = shouldPlay && !wasPlaying;
    const bool stopped = !shouldPlay && wasPlaying;

    if (!juce::exactlyEqual(transport.getTempo(), tempo.load())) {
        transport.setTempo(tempo.load());
    }

    if (started) {
        transport.rewind();
        // Song mode starts at the loop start when looping, else at bar 1; pattern mode at bar 1.
        const int startBar = songModeNow && loopIsActive() ? loopStartBar.load() : 0;
        transport.jump(0, startBar * static_cast<double>(Transport::beatsPerBar));
        for (int i = 0; i < numMachines; i++) {
            slots[static_cast<size_t>(i)].originBeat = 0.0;
        }
    } else if (shouldPlay && songModeNow != wasSongMode && !songModeNow) {
        // Song → pattern while playing: the selected patterns pick up from the current bar.
        const double bar = std::floor(transport.beatAt(transport.getPosition()) / Transport::beatsPerBar);
        for (int i = 0; i < numMachines; i++) {
            auto& slot = slots[static_cast<size_t>(i)];
            slot.originBeat = bar * Transport::beatsPerBar;
            slot.queued = -1;
            publish(i);
        }
    }
    wasPlaying = shouldPlay;
    wasSongMode = songModeNow;

    for (int i = 0; i < numMachines; i++) {
        auto& slot = slots[static_cast<size_t>(i)];
        slot.midi.clear();
        if (stopped) {
            slot.player.releaseAll(slot.midi, 0);
            if (slot.queued >= 0) { // a switch that never reached its boundary happens now
                slot.current = slot.queued;
                slot.queued = -1;
            }
            publish(i);
        }
    }

    const int64_t from = transport.getPosition();
    if (shouldPlay) {
        generateMidi(from, from + numSamples, songModeNow);
    }

    bool anySolo = false;
    for (int i = 0; i < numMachines; i++) {
        anySolo = anySolo || slots[static_cast<size_t>(i)].channel->isSoloed();
    }

    mixBus.clear();
    delayBus.clear();
    reverbBus.clear();

    for (int i = 0; i < numMachines; i++) {
        auto& slot = slots[static_cast<size_t>(i)];
        slot.buffer.setSize(2, numSamples, false, false, true);
        slot.buffer.clear();
        slot.machine->process(slot.buffer, slot.midi);
        for (auto& effect : slot.effects) {
            if (effect != nullptr) {
                effect->process(slot.buffer, numSamples);
            }
        }

        const bool audible = !slot.channel->isMuted() && (!anySolo || slot.channel->isSoloed());
        slot.channel->process(slot.buffer, numSamples, audible);
        slot.channel->addSends(slot.buffer, delayBus, reverbBus, numSamples);
        for (int ch = 0; ch < 2; ch++) {
            mixBus.addFrom(ch, 0, slot.buffer, ch, 0, numSamples);
        }
    }

    master.process(mixBus, delayBus, reverbBus, numSamples, transport.samplesPerBeat());

    if (output.getNumChannels() >= 2) {
        output.copyFrom(0, startSample, mixBus, 0, 0, numSamples);
        output.copyFrom(1, startSample, mixBus, 1, 0, numSamples);
    } else if (output.getNumChannels() == 1) {
        output.copyFrom(0, startSample, mixBus.getReadPointer(0), numSamples, 0.5f);
        output.addFrom(0, startSample, mixBus, 1, 0, numSamples, 0.5f);
    }

    if (shouldPlay) {
        transport.advance(numSamples);
        playheadBeat.store(transport.beatAt(transport.getPosition()));
    }
}

void Engine::generateMidi(int64_t from, int64_t to, bool songModeNow) {
    if (!songModeNow) {
        for (int i = 0; i < numMachines; i++) {
            playPatternMode(slots[static_cast<size_t>(i)], i, from, to, from);
        }
        return;
    }

    // Song mode: the chunk may contain a loop jump or the song's end; split there.
    int64_t position = from;
    while (position < to) {
        int64_t segmentEnd = to;
        bool jumpAtEnd = false;
        bool stopAtEnd = false;

        if (loopIsActive()) {
            const double loopEndBeat = loopEndBar.load() * static_cast<double>(Transport::beatsPerBar);
            const int64_t loopEnd = juce::jmax(position, transport.sampleAt(loopEndBeat));
            if (loopEnd < to) {
                segmentEnd = loopEnd;
                jumpAtEnd = true;
            }
        } else if (const int endBar = songEndBar(); endBar > 0) {
            const int64_t songEnd = juce::jmax(position, transport.sampleAt(endBar * static_cast<double>(Transport::beatsPerBar)));
            if (songEnd < to) {
                segmentEnd = songEnd;
                stopAtEnd = true;
            }
        }

        for (int i = 0; i < numMachines; i++) {
            playSongMode(slots[static_cast<size_t>(i)], i, position, segmentEnd, from);
        }

        if (jumpAtEnd) {
            for (int i = 0; i < numMachines; i++) {
                auto& slot = slots[static_cast<size_t>(i)];
                slot.player.releaseAll(slot.midi, static_cast<int>(segmentEnd - from));
            }
            transport.jump(segmentEnd, loopStartBar.load() * static_cast<double>(Transport::beatsPerBar));
        }
        if (stopAtEnd) {
            playing.store(false); // the next chunk sees the stop and releases what's still held
            return;
        }
        position = segmentEnd;
    }
}

void Engine::playPatternMode(Slot& slot, int index, int64_t from, int64_t to, int64_t blockStart) {
    const Pattern& current = patternAt(slot, slot.current);

    if (slot.queued >= 0) {
        // Switch on the first end-of-pattern at or after `from`.
        const double length = current.lengthInBeats();
        const double elapsed = transport.beatAt(from) - slot.originBeat;
        const double boundaryBeat = slot.originBeat + std::ceil(elapsed / length - 1.0e-9) * length;
        const int64_t boundary = juce::jmax(from, transport.sampleAt(boundaryBeat));

        if (boundary < to) {
            slot.player.process(current, slot.originBeat, transport, from, boundary, blockStart, slot.midi);
            slot.current = slot.queued;
            slot.queued = -1;
            slot.originBeat = boundaryBeat;
            publish(index);
            slot.player.process(patternAt(slot, slot.current), slot.originBeat, transport, boundary, to, blockStart,
                                slot.midi);
            return;
        }
    }

    slot.player.process(current, slot.originBeat, transport, from, to, blockStart, slot.midi);
}

void Engine::playSongMode(Slot& slot, int index, int64_t from, int64_t to, int64_t blockStart) {
    // Walk the range bar by bar: each bar plays the block that covers it (or nothing).
    int64_t position = from;
    while (position < to) {
        const double beat = transport.beatAt(position);
        const int bar = static_cast<int>(std::floor(beat / Transport::beatsPerBar + 1.0e-9));
        const int64_t barEnd = transport.sampleAt((bar + 1) * static_cast<double>(Transport::beatsPerBar));
        const int64_t segmentEnd = barEnd > position ? juce::jmin(to, barEnd) : to;

        const int blockIndex = slot.blocks != nullptr ? Arrangement::indexAt(*slot.blocks, bar) : -1;
        // Song mode shows the block's pattern as playing but leaves the selection (slot.current) alone,
        // so pattern mode resumes where the user left it.
        publishedCurrent[static_cast<size_t>(index)].store(blockIndex >= 0 ? (*slot.blocks)[static_cast<size_t>(blockIndex)].pattern : -1);
        if (blockIndex >= 0) {
            const auto& block = (*slot.blocks)[static_cast<size_t>(blockIndex)];
            slot.player.process(patternAt(slot, block.pattern), block.startBar * static_cast<double>(Transport::beatsPerBar),
                                transport, position, segmentEnd, blockStart, slot.midi);
        } else {
            // No block: nothing starts, but notes still sounding end on time.
            slot.player.process(emptyPattern, 0.0, transport, position, segmentEnd, blockStart, slot.midi);
        }
        position = segmentEnd;
    }
}
