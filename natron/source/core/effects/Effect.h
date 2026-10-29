#pragma once

#include "../Parameter.h"

#include <juce_audio_basics/juce_audio_basics.h>

// An insert effect. Each machine has two effect slots, applied in order between the machine and
// its mixer channel. Same lifetime rules as machines: built and prepared on the message thread,
// handed to the engine by command, deleted through the garbage queue.
class Effect {
public:
    virtual ~Effect() = default;

    virtual juce::String getTypeName() const = 0;
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;

    // Processes the first `numSamples` of a stereo buffer in place.
    virtual void process(juce::AudioBuffer<float>& buffer, int numSamples) = 0;

    virtual void reset() = 0;

    ParameterSet& getParameters() { return parameters; }
    const ParameterSet& getParameters() const { return parameters; }

protected:
    ParameterSet parameters;
};

namespace EffectRegistry {
    juce::StringArray getTypeNames();
    std::unique_ptr<Effect> create(const juce::String& typeName); // nullptr if unknown
}
