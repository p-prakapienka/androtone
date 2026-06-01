#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../PluginProcessor.h"

class MainTab : public juce::Component {
public:
    explicit MainTab(AndrotoneAudioProcessor& p) : processorRef(p) {
        playButton.setButtonText("Play");
        playButton.onClick = [this]() {
            processorRef.setPlaying(!processorRef.isPlaying());
            playButton.setButtonText(processorRef.isPlaying() ? "Stop" : "Play");
        };
        addAndMakeVisible(playButton);

        // Loop on (default) means song mode off: each track just loops its current clip.
        loopButton.setButtonText("Loop");
        loopButton.setClickingTogglesState(true);
        loopButton.setToggleState(!processorRef.isSongMode(), juce::dontSendNotification);
        loopButton.onClick = [this]() {
            processorRef.setSongMode(!loopButton.getToggleState());
        };
        addAndMakeVisible(loopButton);

        tempoSlider.setRange(60.0, 200.0);
        tempoSlider.setValue(processorRef.getTempo());
        tempoSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        tempoSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
        tempoSlider.onValueChange = [this]() {
            processorRef.setTempo(tempoSlider.getValue());
        };
        addAndMakeVisible(tempoSlider);

        tempoLabel.setText("Tempo (BPM):", juce::dontSendNotification);
        tempoLabel.attachToComponent(&tempoSlider, true);
        addAndMakeVisible(tempoLabel);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(10);

        auto buttonArea = bounds.removeFromTop(50);
        playButton.setBounds(buttonArea.getCentreX() - 50, buttonArea.getY(), 100, 40);
        loopButton.setBounds(playButton.getRight() + 10, buttonArea.getY(), 70, 40);

        bounds.removeFromTop(10);
        tempoSlider.setBounds(bounds.removeFromTop(50).withTrimmedLeft(100));
    }

private:
    AndrotoneAudioProcessor& processorRef;
    juce::TextButton playButton;
    juce::TextButton loopButton;
    juce::Slider tempoSlider;
    juce::Label tempoLabel;
};
