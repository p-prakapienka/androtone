#pragma once

#include "core/Engine.h"

#include <juce_audio_utils/juce_audio_utils.h>

// Phase 0 shell: transport controls over the demo song. The rack, editors, song view, and mixer
// screens replace this as they land.
class MainComponent : public juce::AudioAppComponent, private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    Engine engine;

    juce::Label title;
    juce::TextButton playButton { "Play" };
    juce::Slider tempoSlider;
    juce::Label tempoLabel;
    juce::Label positionLabel;

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
