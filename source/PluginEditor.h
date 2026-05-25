#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include "PluginProcessor.h"
#include "Ui/AndrotoneLookAndFeel.h"

class AndrotoneAudioProcessorEditor : public juce::AudioProcessorEditor {
public:
    explicit AndrotoneAudioProcessorEditor(AndrotoneAudioProcessor&);
    ~AndrotoneAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    AndrotoneAudioProcessor& processorRef;
    AndrotoneLookAndFeel lookAndFeel;
    std::unique_ptr<juce::Component> mainTab;
    std::unique_ptr<juce::Component> mixerTab;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AndrotoneAudioProcessorEditor)
};
