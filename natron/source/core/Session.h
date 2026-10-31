#pragma once

#include "Engine.h"
#include "LiveRecorder.h"
#include "Song.h"

#include <deque>
#include <functional>
#include <vector>

// The message-thread side of the app: owns the Song document and keeps the engine in sync by
// posting commands. Every edit the UI makes goes through here, and is safe while audio runs.
// Not thread-safe itself — use it from the message thread only.
class Session {
public:
    explicit Session(Engine& engineToControl);

    static constexpr int maxMachines = Engine::maxMachines;

    const Song& getSong() const { return song; }
    int getNumMachines() const { return static_cast<int>(song.machines.size()); }

    // Returns the new machine's rack index, or -1 (rack full or unknown type).
    int addMachine(const juce::String& type, const juce::String& name = {});
    bool removeMachine(int index);
    bool renameMachine(int index, const juce::String& name);

    bool setPattern(int machine, int patternIndex, const Pattern& pattern);
    const Pattern& getPattern(int machine, int patternIndex) const;
    bool selectPattern(int machine, int patternIndex);

    // Machine parameters.
    bool setParameter(int machine, const juce::String& parameterId, float value);
    float getParameter(int machine, const juce::String& parameterId) const;
    const ParameterSet* getParameters(int machine) const;

    // Mixer channel parameters (see ChannelStrip for ids).
    bool setMixerParameter(int machine, const juce::String& parameterId, float value);
    float getMixerParameter(int machine, const juce::String& parameterId) const;
    const ParameterSet* getMixerParameters(int machine) const;

    // Master bus parameters (see MasterBus for ids).
    bool setMasterParameter(const juce::String& parameterId, float value);
    float getMasterParameter(const juce::String& parameterId) const;
    const ParameterSet& getMasterParameters() const { return engine.getMasterParameters(); }

    // Insert effects: two slots per machine. An empty type clears the slot.
    bool setEffect(int machine, int slot, const juce::String& type);
    bool setEffectParameter(int machine, int slot, const juce::String& parameterId, float value);
    const ParameterSet* getEffectParameters(int machine, int slot) const; // nullptr if the slot is empty

    // Song timeline. The row must be valid (see Arrangement::isValid); edit a copy with the
    // Arrangement functions, then set it.
    bool setBlocks(int machine, const BlockList& blocks);
    const BlockList& getBlocks(int machine) const { return song.machines[static_cast<size_t>(machine)].blocks; }

    // Machine-specific data (samples, formulas...): see Machine::getData. The machine validates it.
    juce::Result setMachineData(int machine, const juce::ValueTree& data);
    juce::ValueTree getMachineData(int machine) const;

    void setSwing(float amount);
    float getSwing() const { return song.swing; }

    // Song automation (song mode). Lanes must target valid machines, with points sorted by beat.
    bool setSongAutomation(const AutomationLanes& lanes);
    const AutomationLanes& getSongAutomation() const { return song.automation; }

    void setSongMode(bool enabled);
    bool isSongMode() const { return song.songMode; }
    // Loop region in bars [startBar, endBar); rejected unless 0 <= startBar < endBar.
    bool setLoop(bool enabled, int startBar, int endBar);

    void setMachineVolume(int machine, float volume) { setMixerParameter(machine, "volume", volume); }
    void setMasterVolume(float volume) { setMasterParameter("volume", volume); }
    void setTempo(double bpm);
    void setPlaying(bool shouldPlay);
    bool isPlaying() const { return engine.isPlaying(); }

    // Replaces the whole song and clears undo history. Stops playback. On failure nothing changes.
    juce::Result loadSong(const Song& newSong);
    juce::Result loadFromStream(juce::InputStream& stream);
    juce::Result saveToStream(juce::OutputStream& stream) const;

    // Frees what the audio thread has finished with; call from a timer.
    void collectGarbage();

    // Recording: while on, notes played live (keyboard, MIDI) on the live target machine are written
    // into its playing pattern, in pattern mode while playing. Starts and lengths snap to 1/16 when
    // quantising. A take (on ... off) is one undo step. Call pollRecording() from a timer.
    void setRecording(bool shouldRecord);
    bool isRecording() const { return engine.isRecording(); }
    void setRecordQuantise(bool quantise) { recorder.grid = quantise ? Pattern::stepLength : 0.0; }
    bool isRecordQuantising() const { return recorder.grid > 0.0; }
    void pollRecording();

    // Undo/redo. Every edit above is undoable except selecting patterns, play/stop and switching
    // pattern/song mode. Edits between beginGesture and endGesture (a drag in an editor) are one
    // step; outside a gesture, repeated edits of the same value (a knob being turned) merge while
    // they come less than coalesceSeconds apart. Undo applies only what differs, so playback goes
    // on; only undoing a machine's removal reloads the rack.
    static constexpr int maxUndoSteps = 100;
    static constexpr double coalesceSeconds = 1.0;
    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }
    bool undo();
    bool redo();
    void beginGesture();
    void endGesture();
    bool isInGesture() const { return gestureDepth > 0; }
    // Ends any open gesture: a safety net for a press whose release never arrived.
    void cancelGestures() { gestureDepth = 1; endGesture(); }
    void clearHistory();
    // Seconds; replaceable so tests control when edits merge.
    std::function<double()> clock = [] { return juce::Time::getMillisecondCounterHiRes() / 1000.0; };

private:
    Engine& engine;
    Song song;

    std::deque<Song> undoStack, redoStack; // songs before each step / after each undone step
    juce::String lastEditKey;              // what the latest step changed, for merging
    double lastEditTime = -1.0e9;
    int gestureDepth = 0;
    bool gestureRecorded = false;
    bool restoring = false;                // applying undo/redo: edits are not recorded
    LiveRecorder recorder;

    // Runs `change` (an edit returning bool / int index / juce::Result) and records an undo step if
    // it succeeded, unless it merges with the previous one (see above).
    template <typename Change>
    auto edit(const juce::String& key, Change&& change);
    void pushUndo(Song before);
    juce::Result replaceSong(const Song& newSong);
    void restore(const Song& target);
    void addRecorded(const LiveRecorder::Recorded& recorded);
    void finishHeldNotes();
    // Live objects, owned by the engine; parallel to song.machines.
    std::vector<Machine*> machines;
    std::vector<ChannelStrip*> channels;
    std::vector<std::array<Effect*, Engine::effectsPerMachine>> effects;

    bool isValidMachine(int index) const { return index >= 0 && index < getNumMachines(); }
    static bool isValidEffectSlot(int slot) { return slot >= 0 && slot < Engine::effectsPerMachine; }
    static bool isValidPatternIndex(int index) { return index >= 0 && index < PatternBank::numPatterns; }
    using EffectSlots = std::array<std::unique_ptr<Effect>, Engine::effectsPerMachine>;

    void postMachine(std::unique_ptr<Machine> machine, std::unique_ptr<ChannelStrip> channel, EffectSlots machineEffects,
                     const MachineState& state, int index);
    void postEffect(int machine, int slot, std::unique_ptr<Effect> effect);
    void syncTransportSettings();
    bool post(const EngineCommand& command);
    bool isValidLane(const AutomationLane& lane, bool songLane) const;
    void postSongAutomation();

    // Sets every value `values` has for a parameter in `parameters`; returns all parameters'
    // resulting (clamped) values, so unknown ids drop out and missing ones get defaults.
    static ParameterValues apply(ParameterSet& parameters, const ParameterValues& values);
    static bool setValue(ParameterSet& parameters, ParameterValues& stored, const juce::String& id, float value);
};
