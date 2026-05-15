#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class AndrotoneAudioProcessorEditor : public juce::AudioProcessorEditor {
public:
    explicit AndrotoneAudioProcessorEditor(AndrotoneAudioProcessor&);
    ~AndrotoneAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    AndrotoneAudioProcessor& processorRef;

    juce::TextButton playButton;
    juce::Slider tempoSlider;
    juce::Slider volumeSlider;
    juce::Label tempoLabel;
    juce::Label volumeLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AndrotoneAudioProcessorEditor)
};
