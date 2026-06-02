#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../PluginProcessor.h"

class ProjectTab : public juce::Component {
public:
    explicit ProjectTab(AndrotoneAudioProcessor& p) : processorRef(p) {
    }

private:
    AndrotoneAudioProcessor& processorRef;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectTab)
};
