#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../PluginProcessor.h"

class SettingsTab : public juce::Component {
public:
    explicit SettingsTab(AndrotoneAudioProcessor& p) : processorRef(p) {
    }

private:
    AndrotoneAudioProcessor& processorRef;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsTab)
};
