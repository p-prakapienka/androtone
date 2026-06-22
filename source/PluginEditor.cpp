#include "PluginEditor.h"

#include "Ui/ControlPanel.h"
#include "Ui/ProjectTab.h"
#include "Ui/MixerTab.h"
#include "Ui/SessionTab.h"
#include "Ui/SettingsTab.h"

AndrotoneAudioProcessorEditor::AndrotoneAudioProcessorEditor(AndrotoneAudioProcessor& p) :
    AudioProcessorEditor(&p), processorRef(p) {

    setLookAndFeel(&lookAndFeel);

    projectTab = std::make_unique<ProjectTab>(processorRef);
    sessionTab = std::make_unique<SessionTab>(processorRef);
    mixerTab = std::make_unique<MixerTab>(processorRef);
    settingsTab = std::make_unique<SettingsTab>(processorRef);

    addAndMakeVisible(tabs);

    const auto tabColour = lookAndFeel.findColour(juce::ResizableWindow::backgroundColourId);
    tabs.addTab("Projects", tabColour, projectTab.get(), false);
    tabs.addTab("Session", tabColour, sessionTab.get(), false);
    tabs.addTab("Mixer", tabColour, mixerTab.get(), false);
    tabs.addTab("Settings", tabColour, settingsTab.get(), false);

    controlPanel = std::make_unique<ControlPanel>(processorRef);
    addAndMakeVisible(*controlPanel);

    setSize(500, 450);
}

AndrotoneAudioProcessorEditor::~AndrotoneAudioProcessorEditor() {
    setLookAndFeel(nullptr);
}

void AndrotoneAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(24.0f));
}

void AndrotoneAudioProcessorEditor::resized() {
    auto bounds = getLocalBounds();
    controlPanel->setBounds(bounds.removeFromBottom(60));
    tabs.setBounds(bounds);
}
