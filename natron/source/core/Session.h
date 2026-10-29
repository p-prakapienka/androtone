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

    const Song& getSong() const { return song; }
    int getNumMachines() const { return static_cast<int>(song.machines.size()); }

    // Returns the new machine's rack index, or -1 (rack full or unknown type).
    int addMachine(const juce::String& type, const juce::String& name = {});
    bool removeMachine(int index);
    bool renameMachine(int index, const juce::String& name);

    bool setPattern(int machine, int patternIndex, const Pattern& pattern);
    const Pattern& getPattern(int machine, int patternIndex) const;
    bool selectPattern(int machine, int patternIndex);

    bool setParameter(int machine, const juce::String& parameterId, float value);
    float getParameter(int machine, const juce::String& parameterId) const;
    const ParameterSet* getParameters(int machine) const;

    void setMachineVolume(int machine, float volume);
    void setTempo(double bpm);
    void setMasterVolume(float volume);
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
    std::vector<Machine*> machines; // live machines, owned by the engine; parallel to song.machines

    bool isValidMachine(int index) const { return index >= 0 && index < getNumMachines(); }
    static bool isValidPatternIndex(int index) { return index >= 0 && index < PatternBank::numPatterns; }
    void postMachine(std::unique_ptr<Machine> machine, const MachineState& state, int index);
};
