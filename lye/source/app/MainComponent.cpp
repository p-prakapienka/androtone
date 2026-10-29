#include "MainComponent.h"

#include "core/DemoSong.h"

MainComponent::MainComponent() {
    loadDemoSong(engine);

    title.setText("Lye", juce::dontSendNotification);
    title.setFont(juce::FontOptions(32.0f, juce::Font::bold));
    title.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(title);

    playButton.setClickingTogglesState(true);
    playButton.onClick = [this] {
        engine.setPlaying(playButton.getToggleState());
        playButton.setButtonText(playButton.getToggleState() ? "Stop" : "Play");
    };
    addAndMakeVisible(playButton);

    tempoSlider.setRange(40.0, 240.0, 1.0);
    tempoSlider.setValue(engine.getTempo(), juce::dontSendNotification);
    tempoSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 32);
    tempoSlider.onValueChange = [this] { engine.setTempo(tempoSlider.getValue()); };
    addAndMakeVisible(tempoSlider);

    tempoLabel.setText("BPM", juce::dontSendNotification);
    tempoLabel.attachToComponent(&tempoSlider, true);

    positionLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(positionLabel);

    setSize(800, 480);
    setAudioChannels(0, 2);
    startTimerHz(30);
}

MainComponent::~MainComponent() {
    shutdownAudio();
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    engine.prepare(sampleRate, samplesPerBlockExpected);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
    auto* const* channels = bufferToFill.buffer->getArrayOfWritePointers();
    juce::AudioBuffer<float> region(channels, bufferToFill.buffer->getNumChannels(), bufferToFill.startSample,
                                    bufferToFill.numSamples);
    engine.process(region);
}

void MainComponent::releaseResources() {}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1d1f21));
}

void MainComponent::resized() {
    auto area = getLocalBounds().reduced(24);
    title.setBounds(area.removeFromTop(60));
    area.removeFromTop(16);
    playButton.setBounds(area.removeFromTop(64).withSizeKeepingCentre(200, 64));
    area.removeFromTop(16);
    tempoSlider.setBounds(area.removeFromTop(48).withTrimmedLeft(60));
    area.removeFromTop(16);
    positionLabel.setBounds(area.removeFromTop(32));
}

void MainComponent::timerCallback() {
    const double beat = engine.getPlayheadBeat();
    const int bar = static_cast<int>(beat / Transport::beatsPerBar) + 1;
    const int beatInBar = static_cast<int>(std::fmod(beat, Transport::beatsPerBar)) + 1;
    positionLabel.setText(juce::String(bar) + "." + juce::String(beatInBar), juce::dontSendNotification);
}
