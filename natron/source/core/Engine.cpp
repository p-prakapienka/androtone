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
        delete item.machine;
        delete item.pattern;
        delete item.channel;
        item = {};
    });
}

void Engine::deletePayload(const EngineCommand& command) {
    delete command.newMachine;
    delete command.newPattern;
    delete command.newChannel;
}

void Engine::trash(Garbage item) {
    if (item.machine == nullptr && item.pattern == nullptr && item.channel == nullptr) {
        return;
    }
    if (garbageFifo.getFreeSpace() < 1) {
        jassertfalse; // collectGarbage() isn't being called; leak rather than free on the audio thread
        return;
    }
    garbageFifo.write(1).forEach([&](int i) { garbage[static_cast<size_t>(i)] = item; });
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
    const bool validPattern = command.pattern >= 0 && command.pattern < PatternBank::numPatterns;

    switch (command.type) {
        case EngineCommand::Type::addMachine:
            if (numMachines < maxMachines && command.newMachine != nullptr && command.newChannel != nullptr) {
                auto& slot = slots[static_cast<size_t>(numMachines)];
                slot.machine.reset(command.newMachine);
                slot.channel.reset(command.newChannel);
                publish(numMachines);
                numMachines++;
            } else {
                trash({ command.newMachine, nullptr, command.newChannel });
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
                auto*& target = slots[static_cast<size_t>(command.machine)].patterns[static_cast<size_t>(command.pattern)];
                trash({ nullptr, target, nullptr });
                target = command.newPattern;
            } else {
                trash({ nullptr, command.newPattern, nullptr });
            }
            break;

        case EngineCommand::Type::selectPattern:
            if (validMachine && validPattern) {
                auto& slot = slots[static_cast<size_t>(command.machine)];
                if (wasPlaying && command.pattern != slot.current) {
                    slot.queued = command.pattern;
                } else {
                    slot.current = command.pattern;
                    slot.queued = -1;
                }
                publish(command.machine);
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
    trash({ slot.machine.release(), nullptr, slot.channel.release() });
    for (auto*& pattern : slot.patterns) {
        trash({ nullptr, pattern, nullptr });
        pattern = nullptr;
    }
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
    const bool started = shouldPlay && !wasPlaying;
    const bool stopped = !shouldPlay && wasPlaying;
    wasPlaying = shouldPlay;

    if (started) {
        transport.rewind();
        for (int i = 0; i < numMachines; i++) {
            slots[static_cast<size_t>(i)].originBeat = 0.0;
        }
    }
    if (!juce::exactlyEqual(transport.getTempo(), tempo.load())) {
        transport.setTempo(tempo.load());
    }

    const int64_t from = transport.getPosition();

    bool anySolo = false;
    for (int i = 0; i < numMachines; i++) {
        anySolo = anySolo || slots[static_cast<size_t>(i)].channel->isSoloed();
    }

    mixBus.clear();
    delayBus.clear();
    reverbBus.clear();

    for (int i = 0; i < numMachines; i++) {
        auto& slot = slots[static_cast<size_t>(i)];
        slot.midi.clear();

        if (stopped) {
            slot.player.releaseAll(slot.midi, 0);
            if (slot.queued >= 0) { // a switch that never reached its boundary happens now
                slot.current = slot.queued;
                slot.queued = -1;
                publish(i);
            }
        } else if (shouldPlay) {
            playSlot(slot, i, from, from + numSamples);
        }

        slot.buffer.setSize(2, numSamples, false, false, true);
        slot.buffer.clear();
        slot.machine->process(slot.buffer, slot.midi);

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

void Engine::playSlot(Slot& slot, int index, int64_t from, int64_t to) {
    const Pattern& current = patternAt(slot, slot.current);

    if (slot.queued >= 0) {
        // Switch on the first end-of-pattern at or after `from`.
        const double length = current.lengthInBeats();
        const double elapsed = transport.beatAt(from) - slot.originBeat;
        const double boundaryBeat = slot.originBeat + std::ceil(elapsed / length - 1.0e-9) * length;
        const int64_t boundary = juce::jmax(from, transport.sampleAt(boundaryBeat));

        if (boundary < to) {
            slot.player.process(current, slot.originBeat, transport, from, boundary, from, slot.midi);
            slot.current = slot.queued;
            slot.queued = -1;
            slot.originBeat = boundaryBeat;
            publish(index);
            slot.player.process(patternAt(slot, slot.current), slot.originBeat, transport, boundary, to, from,
                                slot.midi);
            return;
        }
    }

    slot.player.process(current, slot.originBeat, transport, from, to, from, slot.midi);
}
