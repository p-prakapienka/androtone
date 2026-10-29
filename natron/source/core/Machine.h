#pragma once

#include "Parameter.h"

#include <juce_audio_basics/juce_audio_basics.h>

// One instrument in the rack (Caustic calls these machines). The engine gives each machine a
// stereo buffer per block, already cleared, plus the MIDI its pattern produced for that block.
class Machine {
public:
    virtual ~Machine() = default;

    // Stable type id, used in song files and by MachineRegistry.
    virtual juce::String getTypeName() const = 0;

    // Called off the audio thread, before the machine is handed to the engine and whenever the
    // device changes.
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;

    // Renders one block into `output` (stereo, cleared), consuming `midi` (block-relative positions).
    virtual void process(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) = 0;

    // Silences the machine immediately, dropping voices and tails.
    virtual void reset() = 0;

    ParameterSet& getParameters() { return parameters; }
    const ParameterSet& getParameters() const { return parameters; }

protected:
    ParameterSet parameters;
};
