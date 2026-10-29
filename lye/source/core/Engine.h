#pragma once

#include "Machine.h"
#include "Pattern.h"
#include "PatternPlayer.h"
#include "Transport.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>

// The rack: up to 14 machines, each with a pattern, a pattern player, and a channel on a simple
// mixer. process() is the whole audio callback.
//
// Threading: transport and mixer setters are atomics, safe from any thread. Rack and pattern
// editing (addMachine, getPattern) is NOT real-time safe yet — do it before audio starts. A
// lock-free command queue for live edits is Phase 1 work (see docs/roadmap.md).
class Engine {
public:
    static constexpr int maxMachines = 14;

    Engine();

    // Returns the machine's rack slot, or -1 if the rack is full.
    int addMachine(std::unique_ptr<Machine> machine);
    int getNumMachines() const { return numMachines; }
    Machine& getMachine(int index) { return *slots[static_cast<size_t>(index)].machine; }
    Pattern& getPattern(int index) { return slots[static_cast<size_t>(index)].pattern; }

    void prepare(double sampleRate, int maxBlockSize);
    void process(juce::AudioBuffer<float>& output);

    void setPlaying(bool shouldPlay) { playing.store(shouldPlay); }
    bool isPlaying() const { return playing.load(); }
    void setTempo(double bpm) { tempo.store(juce::jlimit(20.0, 400.0, bpm)); }
    double getTempo() const { return tempo.load(); }
    void setMachineVolume(int index, float volume) { slots[static_cast<size_t>(index)].volume.store(volume); }
    float getMachineVolume(int index) const { return slots[static_cast<size_t>(index)].volume.load(); }
    void setMasterVolume(float volume) { masterVolume.store(volume); }
    float getMasterVolume() const { return masterVolume.load(); }

    // Playhead in beats since play was pressed, for the UI.
    double getPlayheadBeat() const { return playheadBeat.load(); }

private:
    struct Slot {
        std::unique_ptr<Machine> machine;
        Pattern pattern;
        PatternPlayer player;
        std::atomic<float> volume { 0.8f };
        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer midi;
    };

    std::array<Slot, maxMachines> slots;
    int numMachines = 0;

    Transport transport;
    double sampleRate = 48000.0;
    int maxBlock = 512;
    bool wasPlaying = false;

    std::atomic<bool> playing { false };
    std::atomic<double> tempo { 120.0 };
    std::atomic<float> masterVolume { 0.8f };
    std::atomic<double> playheadBeat { 0.0 };

    void processChunk(juce::AudioBuffer<float>& output, int startSample, int numSamples);
};
