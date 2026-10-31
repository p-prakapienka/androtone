#pragma once

#include "Arrangement.h"
#include "Machine.h"
#include "Pattern.h"
#include "PatternPlayer.h"
#include "Transport.h"
#include "effects/Effect.h"
#include "mixer/ChannelStrip.h"
#include "mixer/MasterBus.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

// An edit for the audio thread. Pointer payloads are owned by the command until the engine applies
// it; replaced objects come back through the garbage queue and are deleted by collectGarbage().
struct EngineCommand {
    enum class Type {
        addMachine,    // newMachine + newChannel → next free rack slot
        removeMachine, // machine; later slots move up one
        clearRack,
        setPattern,    // newPattern → machine's bank slot `index` (nullptr = empty)
        selectPattern, // machine plays pattern `index`: now when stopped, at the current pattern's end when playing
        setEffect,     // newEffect → machine's effect slot `index` (nullptr = none)
        setBlocks,     // newBlocks → machine's song timeline row (nullptr = empty)
        setSongAutomation, // newAutomation → the song's automation lanes (nullptr = none)
    };

    Type type = Type::clearRack;
    int machine = -1;
    int index = -1;
    Machine* newMachine = nullptr;
    ChannelStrip* newChannel = nullptr;
    Pattern* newPattern = nullptr;
    Effect* newEffect = nullptr;
    BlockList* newBlocks = nullptr;
    AutomationLanes* newAutomation = nullptr;
};

// The audio engine: a rack of up to 14 machines, each with 64 patterns (4 banks of 16), two insert
// effects, a song timeline row, a pattern player, and a mixer channel, feeding the master bus.
// process() is the whole audio callback.
//
// Two playback modes: pattern mode loops each machine's selected pattern; song mode follows the
// timeline blocks, looping the loop region if enabled and otherwise stopping at the song's end.
//
// Threading:
//  - process() runs on the audio thread; prepare() runs while no callback is running (it also
//    applies queued commands, so it must not overlap process()).
//  - post() and collectGarbage() are for ONE other thread (the message thread): the queues are
//    single-producer / single-consumer. Session is the intended caller.
//  - Transport and status getters/setters are atomics, safe from any thread. Machine, effect,
//    channel and master parameters are atomics inside their ParameterSets (see Session).
// Nothing in process() allocates, frees, or locks.
class Engine {
public:
    static constexpr int maxMachines = 14;
    static constexpr int effectsPerMachine = 2;

    Engine();
    ~Engine();

    // Queues an edit, applied at the start of the next process() call. Returns false (and deletes
    // the payload) if the queue is full.
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

    // 0.5 (straight) ... 0.75; see Song::swing.
    void setSwing(float amount) { swing.store(juce::jlimit(0.5f, 0.75f, amount)); }
    float getSwing() const { return swing.load(); }

    void setSongMode(bool enabled) { songMode.store(enabled); }
    bool isSongMode() const { return songMode.load(); }
    // Song-mode loop region in bars [startBar, endBar). Ignored unless endBar > startBar.
    void setLoop(bool enabled, int startBar, int endBar) {
        loopStartBar.store(startBar);
        loopEndBar.store(endBar);
        loopEnabled.store(enabled);
    }

    // Live input (MIDI devices, the on-screen keyboard): note and controller messages played on the
    // target machine at the start of the next block. Safe from any thread, including several at
    // once. Returns false if the queue is full (the message is dropped).
    bool pushLiveMidi(const juce::MidiMessage& message);
    void setLiveTarget(int machine) { liveTarget.store(machine); }
    int getLiveTarget() const { return liveTarget.load(); }

    // Recording: while on, playing, and in pattern mode, live notes played on the target machine are
    // reported (with where they fell in its playing pattern) for Session to write into the pattern.
    struct RecordedEvent {
        int machine = 0;
        int pattern = 0;
        double patternBeat = 0.0; // position in the pattern, [0, length)
        double beat = 0.0;        // transport beat, for lengths across loops
        double patternLength = 4.0; // beats
        int pitch = 60;
        float velocity = 0.0f;    // 0 = note off
    };
    void setRecording(bool shouldRecord) { recording.store(shouldRecord); }
    bool isRecording() const { return recording.load(); }
    // The next recorded event, if any. For the posting thread (see post()).
    bool popRecordedEvent(RecordedEvent& event);

    // The master section's parameters; the bus itself lives for the engine's lifetime.
    ParameterSet& getMasterParameters() { return master.getParameters(); }

    // Status published by the audio thread, for the UI and tests.
    int getNumMachines() const { return publishedNumMachines.load(); }
    // The pattern playing (pattern mode: the selected one; song mode: the current block's, or -1).
    int getCurrentPattern(int machine) const { return publishedCurrent[static_cast<size_t>(machine)].load(); }
    int getQueuedPattern(int machine) const { return publishedQueued[static_cast<size_t>(machine)].load(); }
    double getPlayheadBeat() const { return playheadBeat.load(); }

private:
    struct Slot {
        std::unique_ptr<Machine> machine;
        std::unique_ptr<ChannelStrip> channel;
        std::array<std::unique_ptr<Effect>, effectsPerMachine> effects;
        std::array<Pattern*, PatternBank::numPatterns> patterns {}; // owned; nullptr = empty pattern
        BlockList* blocks = nullptr;                                // owned; nullptr = empty row
        int current = 0;
        int queued = -1;
        double originBeat = 0.0; // pattern mode: where the current pattern started looping
        PatternPlayer player;
        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer midi;
    };

    // Type-erased, allocation-free deferred delete.
    struct Garbage {
        void* object = nullptr;
        void (*destroy)(void*) = nullptr;
    };

    std::array<Slot, maxMachines> slots;
    int numMachines = 0;
    AutomationLanes* songAutomation = nullptr; // owned

    juce::AbstractFifo commandFifo { 8192 };
    std::vector<EngineCommand> commands;
    juce::AbstractFifo garbageFifo { 16384 };
    std::vector<Garbage> garbage;

    struct LiveEvent {
        juce::uint8 bytes[3] {};
        int size = 0;
    };
    juce::AbstractFifo liveFifo { 1024 };
    std::array<LiveEvent, 1024> liveEvents;
    juce::SpinLock liveWriters;      // serialises producers; the audio thread never takes it
    std::atomic<int> liveTarget { 0 };
    juce::MidiBuffer liveMidi;       // drained queue, for the current block
    std::atomic<bool> recording { false };
    juce::AbstractFifo recordFifo { 1024 };
    std::array<RecordedEvent, 1024> recordedEvents;

    Transport transport;
    bool wasPlaying = false;
    bool wasSongMode = false;

    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<int> maxBlock { 512 };
    std::atomic<bool> playing { false };
    std::atomic<double> tempo { 120.0 };
    std::atomic<bool> songMode { false };
    std::atomic<float> swing { 0.5f };
    float swingNow = 0.5f; // read once per chunk
    std::atomic<bool> loopEnabled { false };
    std::atomic<int> loopStartBar { 0 };
    std::atomic<int> loopEndBar { 0 };
    MasterBus master;
    juce::AudioBuffer<float> mixBus;
    juce::AudioBuffer<float> delayBus;
    juce::AudioBuffer<float> reverbBus;
    std::atomic<int> publishedNumMachines { 0 };
    std::array<std::atomic<int>, maxMachines> publishedCurrent;
    std::array<std::atomic<int>, maxMachines> publishedQueued;
    std::atomic<double> playheadBeat { 0.0 };

    void applyCommands();
    void apply(const EngineCommand& command);
    void removeSlot(int index);
    void clearSlot(Slot& slot);
    void publish(int index);
    static void deletePayload(const EngineCommand& command);

    template <typename T>
    void trash(T* object) {
        if (object == nullptr) {
            return;
        }
        if (garbageFifo.getFreeSpace() < 1) {
            jassertfalse; // collectGarbage() isn't being called; leak rather than free on the audio thread
            return;
        }
        garbageFifo.write(1).forEach([&](int i) {
            garbage[static_cast<size_t>(i)] = { object, [](void* p) { delete static_cast<T*>(p); } };
        });
    }

    const Pattern& patternAt(const Slot& slot, int index) const;
    bool loopIsActive() const;
    int songEndBar() const;
    void processChunk(juce::AudioBuffer<float>& output, int startSample, int numSamples);
    void generateMidi(int64_t from, int64_t to, bool songModeNow);
    void playPatternMode(Slot& slot, int index, int64_t from, int64_t to, int64_t blockStart);
    void playSongMode(Slot& slot, int index, int64_t from, int64_t to, int64_t blockStart);
    void applyAutomation(double beat, bool songModeNow);
    void drainLiveMidi();
    void recordLiveMidi(int target, int64_t position);
    ParameterSet* targetParameters(const AutomationTarget& target, int machine);
    static void write(ParameterSet* parameters, const AutomationLane& lane, double beat);
};
