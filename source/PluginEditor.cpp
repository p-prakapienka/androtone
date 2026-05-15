#include "PluginEditor.h"

AndrotoneAudioProcessorEditor::AndrotoneAudioProcessorEditor(AndrotoneAudioProcessor& p) : AudioProcessorEditor(&p),
    processorRef(p) {

    setSize(400, 300);

    playButton.setButtonText("Play");
    playButton.onClick = [this]() {
        processorRef.setPlaying(!processorRef.isPlaying());
        playButton.setButtonText(processorRef.isPlaying() ? "Stop" : "Play");
    };
    addAndMakeVisible(playButton);

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

    volumeSlider.setRange(0.0, 1.0);
    volumeSlider.setValue(processorRef.getVolume());
    volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    volumeSlider.onValueChange = [this]() {
        processorRef.setVolume((float) volumeSlider.getValue());
    };
    addAndMakeVisible(volumeSlider);

    volumeLabel.setText("Volume:", juce::dontSendNotification);
    volumeLabel.attachToComponent(&volumeSlider, true);
    addAndMakeVisible(volumeLabel);
}

void AndrotoneAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(24.0f));
    g.drawFittedText("Androtone", juce::Rectangle<int>(0, 10, getWidth(), 40), juce::Justification::centred, 1);
}

void AndrotoneAudioProcessorEditor::resized() {
    auto bounds = getLocalBounds().reduced(10);

    auto titleArea = bounds.removeFromTop(40);
    bounds.removeFromTop(10);

    auto buttonArea = bounds.removeFromTop(50);
    playButton.setBounds(buttonArea.getCentreX() - 50, buttonArea.getY(), 100, 40);

    bounds.removeFromTop(10);
    tempoSlider.setBounds(bounds.removeFromTop(50).withTrimmedLeft(100));

    bounds.removeFromTop(5);
    volumeSlider.setBounds(bounds.removeFromTop(50).withTrimmedLeft(100));
}
