#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// Interface for an audio processor (an effect): something that consumes audio
// and produces audio in place. Concrete effects (Reverb, ...) implement this so
// the call site can hold a Processor* / std::unique_ptr<Processor> and swap one
// effect for another without knowing the concrete type.
class Processor {
public:
    virtual ~Processor() = default;

    // Called once the sample rate is known, before the first process() call.
    virtual void prepare(double sampleRate) = 0;

    // Processes one stereo sample frame in place: left and right are replaced
    // with the processed (wet) output.
    virtual void process(float& left, float& right) = 0;

    // Clears internal state / tails, e.g. on sample-rate change or transport stop.
    virtual void reset() = 0;
};
