#pragma once

#include "Machine.h"
#include "Pattern.h"
#include "PatternPlayer.h"
#include "Transport.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

// An edit for the audio thread. Pointer payloads are owned by the command until the engine applies
// it; replaced objects come back through the garbage queue and are deleted by collectGarbage().
struct EngineCommand {
    enum class Type {
        addMachine,    // newMachine → next free rack slot
        removeMachine, // machine; later slots move up one
        clearRack,
        setPattern,    // newPattern → machine's bank slot `pattern` (nullptr = empty)
        selectPattern, // machine plays `pattern`: now when stopped, at the current pattern's end when playing
    };

    Type type = Type::clearRack;
    int machine = -1;
    int pattern = -1;
    Machine* newMachine = nullptr;
    Pattern* newPattern = nullptr;
};

// The audio engine: a rack of up to 14 machines, each with 64 patterns (4 banks of 16), a pattern
// player, and a mixer channel. process() is the whole audio callback.
//
// Threading:
//  - process() and prepare() run on the audio thread (prepare while no callback is running).
//  - post() and collectGarbage() are for ONE other thread (the message thread): the queues are
//    single-producer / single-consumer. Session is the intended caller.
//  - Transport, mixer, and status getters/setters are atomics, safe from any thread.
// Nothing in process() allocates, frees, or locks.
class Engine {
public:
    static constexpr int maxMachines = 14;

    Engine();
    ~Engine();

    // Queues an edit, applied at the start of the next process() call. Returns false (and keeps
    // ownership of nothing — the payload is deleted) if the queue is full.
    bool post(const EngineCommand& command);

    // Deletes objects the audio thread has finished with. Call regularly from the posting thread.
    void collectGarbage();

    void prepare(double sampleRate, int maxBlockSize);
    void process(juce::AudioBuffer<float>& output);

    double getSampleRate() const { return sampleRate.load(); }
    int getMaxBlockSize() const { return maxBlock.load(); }

    void setPlaying(bool shouldPlay) { playing.store(shouldPlay); }
    bool isPlaying() const { return playing.load(); }
    void setTempo(double bpm) { tempo.store(juce::jlimit(20.0, 400.0, bpm)); }
    double getTempo() const { return tempo.load(); }
    void setMachineVolume(int index, float volume) { volumes[static_cast<size_t>(index)].store(volume); }
    float getMachineVolume(int index) const { return volumes[static_cast<size_t>(index)].load(); }
    void setMasterVolume(float volume) { masterVolume.store(volume); }
    float getMasterVolume() const { return masterVolume.load(); }

    // Status published by the audio thread, for the UI and tests.
    int getNumMachines() const { return publishedNumMachines.load(); }
    int getCurrentPattern(int machine) const { return publishedCurrent[static_cast<size_t>(machine)].load(); }
    int getQueuedPattern(int machine) const { return publishedQueued[static_cast<size_t>(machine)].load(); }
    double getPlayheadBeat() const { return playheadBeat.load(); }

private:
    struct Slot {
        std::unique_ptr<Machine> machine;
        std::array<Pattern*, PatternBank::numPatterns> patterns {}; // owned; nullptr = empty pattern
        int current = 0;
        int queued = -1;
        double originBeat = 0.0; // where the current pattern started looping
        PatternPlayer player;
        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer midi;
    };

    struct Garbage {
        Machine* machine = nullptr;
        Pattern* pattern = nullptr;
    };

    std::array<Slot, maxMachines> slots;
    int numMachines = 0;

    juce::AbstractFifo commandFifo { 8192 };
    std::vector<EngineCommand> commands;
    juce::AbstractFifo garbageFifo { 16384 };
    std::vector<Garbage> garbage;

    Transport transport;
    bool wasPlaying = false;

    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<int> maxBlock { 512 };
    std::atomic<bool> playing { false };
    std::atomic<double> tempo { 120.0 };
    std::atomic<float> masterVolume { 0.8f };
    std::array<std::atomic<float>, maxMachines> volumes;
    std::atomic<int> publishedNumMachines { 0 };
    std::array<std::atomic<int>, maxMachines> publishedCurrent;
    std::array<std::atomic<int>, maxMachines> publishedQueued;
    std::atomic<double> playheadBeat { 0.0 };

    void applyCommands();
    void apply(const EngineCommand& command);
    void removeSlot(int index);
    void clearSlot(Slot& slot);
    void publish(int index);
    void trash(Machine* machine);
    void trash(Pattern* pattern);
    static void deletePayload(const EngineCommand& command);

    const Pattern& patternAt(const Slot& slot, int index) const;
    void processChunk(juce::AudioBuffer<float>& output, int startSample, int numSamples);
    void playSlot(Slot& slot, int index, int64_t from, int64_t to);
};
