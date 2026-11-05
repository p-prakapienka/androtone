#include "PluginEditor.h"

#include "Ui/ArrangeTab.h"
#include "Ui/ClipTab.h"
#include "Ui/ControlPanel.h"
#include "Ui/ProjectTab.h"
#include "Ui/MixerTab.h"
#include "Ui/SessionTab.h"
#include "Ui/SettingsTab.h"

AndrotoneAudioProcessorEditor::AndrotoneAudioProcessorEditor(AndrotoneAudioProcessor& p) :
    AudioProcessorEditor(&p), processorRef(p) {

    setLookAndFeel(&lookAndFeel);

    projectTab = std::make_unique<ProjectTab>(processorRef);
    auto session = std::make_unique<SessionTab>(processorRef);
    session->onOpenClip = [this](int trackIndex, int clipIndex) { openClip(trackIndex, clipIndex); };
    sessionTab = std::move(session);
    clipTab = std::make_unique<ClipTab>(processorRef);
    arrangeTab = std::make_unique<ArrangeTab>(processorRef);
    mixerTab = std::make_unique<MixerTab>(processorRef);
    settingsTab = std::make_unique<SettingsTab>(processorRef);

    addAndMakeVisible(tabs);

    const auto tabColour = lookAndFeel.findColour(juce::ResizableWindow::backgroundColourId);
    tabs.addTab("Projects", tabColour, projectTab.get(), false);
    tabs.addTab("Session", tabColour, sessionTab.get(), false);
    tabs.addTab("Arrange", tabColour, arrangeTab.get(), false);
    tabs.addTab("Clip", tabColour, clipTab.get(), false);
    tabs.addTab("Mixer", tabColour, mixerTab.get(), false);
    tabs.addTab("Settings", tabColour, settingsTab.get(), false);

    controlPanel = std::make_unique<ControlPanel>(processorRef);
    addAndMakeVisible(*controlPanel);

    setResizable(true, true);
    setResizeLimits(480, 420, 2400, 1600);
    setSize(900, 620);
    tabs.setCurrentTabIndex(1);
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

void AndrotoneAudioProcessorEditor::openClip(int trackIndex, int clipIndex) {
    static_cast<ClipTab*>(clipTab.get())->showClip(trackIndex, clipIndex);
    tabs.setCurrentTabIndex(3);
}
