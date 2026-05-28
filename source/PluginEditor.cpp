#include "PluginEditor.h"

#include "Ui/MainTab.h"
#include "Ui/MixerTab.h"
#include "Ui/SessionTab.h"

AndrotoneAudioProcessorEditor::AndrotoneAudioProcessorEditor(AndrotoneAudioProcessor& p) :
    AudioProcessorEditor(&p), processorRef(p) {

    setLookAndFeel(&lookAndFeel);

    setSize(500, 360);

    mainTab = std::make_unique<MainTab>(processorRef);
    sessionTab = std::make_unique<SessionTab>(processorRef);
    mixerTab = std::make_unique<MixerTab>(processorRef);

    const auto tabColour = lookAndFeel.findColour(juce::ResizableWindow::backgroundColourId);
    tabs.addTab("Main", tabColour, mainTab.get(), false);
    tabs.addTab("Session", tabColour, sessionTab.get(), false);
    tabs.addTab("Mixer", tabColour, mixerTab.get(), false);
    addAndMakeVisible(tabs);
}

AndrotoneAudioProcessorEditor::~AndrotoneAudioProcessorEditor() {
    setLookAndFeel(nullptr);
}

void AndrotoneAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(24.0f));
    g.drawFittedText("Androtone", juce::Rectangle<int>(0, 10, getWidth(), 40), juce::Justification::centred, 1);
}

void AndrotoneAudioProcessorEditor::resized() {
    auto bounds = getLocalBounds().reduced(10);
    bounds.removeFromTop(40);
    bounds.removeFromTop(5);
    tabs.setBounds(bounds);
}
