#pragma once

#include "Engine.h"
#include "Song.h"

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

    void setSongMode(bool enabled);
    bool isSongMode() const { return song.songMode; }
    // Loop region in bars [startBar, endBar); rejected unless 0 <= startBar < endBar.
    bool setLoop(bool enabled, int startBar, int endBar);

    void setMachineVolume(int machine, float volume) { setMixerParameter(machine, "volume", volume); }
    void setMasterVolume(float volume) { setMasterParameter("volume", volume); }
    void setTempo(double bpm);
    void setPlaying(bool shouldPlay) { engine.setPlaying(shouldPlay); }
    bool isPlaying() const { return engine.isPlaying(); }

    // Replaces the whole song. Stops playback. On failure nothing changes.
    juce::Result loadSong(const Song& newSong);
    juce::Result loadFromStream(juce::InputStream& stream);
    juce::Result saveToStream(juce::OutputStream& stream) const;

    // Frees what the audio thread has finished with; call from a timer.
    void collectGarbage() { engine.collectGarbage(); }

private:
    Engine& engine;
    Song song;
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

    // Sets every value `values` has for a parameter in `parameters`; returns all parameters'
    // resulting (clamped) values, so unknown ids drop out and missing ones get defaults.
    static ParameterValues apply(ParameterSet& parameters, const ParameterValues& values);
    static bool setValue(ParameterSet& parameters, ParameterValues& stored, const juce::String& id, float value);
};
