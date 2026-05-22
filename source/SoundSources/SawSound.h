#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class SawSound : public juce::SynthesiserSound {
public:
    explicit SawSound(int channel) : channel(channel) {}

    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int ch) override { return ch == channel; }

private:
    int channel;
};
