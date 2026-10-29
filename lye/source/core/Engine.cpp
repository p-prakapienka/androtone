#include "Engine.h"

Engine::Engine() = default;

int Engine::addMachine(std::unique_ptr<Machine> machine) {
    if (numMachines >= maxMachines || machine == nullptr) {
        return -1;
    }
    auto& slot = slots[static_cast<size_t>(numMachines)];
    slot.machine = std::move(machine);
    slot.machine->prepare(sampleRate, maxBlock);
    slot.buffer.setSize(2, maxBlock);
    return numMachines++;
}

void Engine::prepare(double newSampleRate, int maxBlockSize) {
    sampleRate = newSampleRate;
    maxBlock = juce::jmax(1, maxBlockSize);
    transport.prepare(sampleRate);
    transport.setTempo(tempo.load());
    wasPlaying = false;

    for (auto& slot : slots) {
        slot.buffer.setSize(2, maxBlock);
        slot.midi.ensureSize(4096);
        if (slot.machine != nullptr) {
            slot.machine->prepare(sampleRate, maxBlock);
        }
    }
}

void Engine::process(juce::AudioBuffer<float>& output) {
    juce::ScopedNoDenormals noDenormals;
    output.clear();

    // Hosts (and Android in particular) may hand over more than they announced; work in chunks
    // no larger than what the machines were prepared for.
    for (int start = 0; start < output.getNumSamples(); start += maxBlock) {
        processChunk(output, start, juce::jmin(maxBlock, output.getNumSamples() - start));
    }
}

void Engine::processChunk(juce::AudioBuffer<float>& output, int startSample, int numSamples) {
    const bool shouldPlay = playing.load();
    const bool started = shouldPlay && !wasPlaying;
    const bool stopped = !shouldPlay && wasPlaying;
    wasPlaying = shouldPlay;

    if (started) {
        transport.rewind();
    }
    if (!juce::exactlyEqual(transport.getTempo(), tempo.load())) {
        transport.setTempo(tempo.load());
    }

    const float master = masterVolume.load();
    const int numOutputChannels = juce::jmin(2, output.getNumChannels());

    for (int i = 0; i < numMachines; i++) {
        auto& slot = slots[static_cast<size_t>(i)];
        slot.midi.clear();

        if (stopped) {
            slot.player.releaseAll(slot.midi, 0);
        } else if (shouldPlay) {
            slot.player.process(slot.pattern, transport, transport.getPosition(), numSamples, slot.midi);
        }

        slot.buffer.setSize(2, numSamples, false, false, true);
        slot.buffer.clear();
        slot.machine->process(slot.buffer, slot.midi);

        const float gain = slot.volume.load() * master;
        for (int ch = 0; ch < numOutputChannels; ch++) {
            output.addFrom(ch, startSample, slot.buffer, ch, 0, numSamples, gain);
        }
    }

    if (shouldPlay) {
        transport.advance(numSamples);
        playheadBeat.store(transport.beatAt(transport.getPosition()));
    }
}
