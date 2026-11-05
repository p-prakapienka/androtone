#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include "PluginProcessor.h"
#include "Ui/AndrotoneLookAndFeel.h"

class AndrotoneAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    void openClip(int trackIndex, int clipIndex);

    explicit AndrotoneAudioProcessorEditor(AndrotoneAudioProcessor&);
    ~AndrotoneAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void buildTabs();

    AndrotoneAudioProcessor& processorRef;
    int shownSetVersion = 0;
    AndrotoneLookAndFeel lookAndFeel;
    std::unique_ptr<juce::Component> projectTab;
    std::unique_ptr<juce::Component> sessionTab;
    std::unique_ptr<juce::Component> clipTab;
    std::unique_ptr<juce::Component> arrangeTab;
    std::unique_ptr<juce::Component> mixerTab;
    std::unique_ptr<juce::Component> settingsTab;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<juce::Component> controlPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AndrotoneAudioProcessorEditor)
};
