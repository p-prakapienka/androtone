#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// One instrument in the rack (Caustic calls these machines). The engine gives each machine a
// stereo buffer per block, already cleared, plus the MIDI its pattern produced for that block.
class Machine {
public:
    virtual ~Machine() = default;

    virtual juce::String getTypeName() const = 0;

    virtual void prepare(double sampleRate, int maxBlockSize) = 0;

    // Renders one block into `output` (stereo, cleared), consuming `midi` (block-relative positions).
    virtual void process(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) = 0;

    // Silences the machine immediately, dropping voices and tails.
    virtual void reset() = 0;
};
