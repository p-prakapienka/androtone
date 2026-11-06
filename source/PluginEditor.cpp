#include "PluginEditor.h"

#include "Ui/ArrangeTab.h"
#include "Ui/ClipTab.h"
#include "Ui/ControlPanel.h"
#include "Ui/DeviceTab.h"
#include "Ui/ProjectTab.h"
#include "Ui/MixerTab.h"
#include "Ui/SessionTab.h"
#include "Ui/SettingsTab.h"

CliptoneAudioProcessorEditor::CliptoneAudioProcessorEditor(CliptoneAudioProcessor& p) :
    AudioProcessorEditor(&p), processorRef(p) {

    setLookAndFeel(&lookAndFeel);

    addAndMakeVisible(tabs);
    buildTabs();
    tabs.setCurrentTabIndex(1);

    controlPanel = std::make_unique<ControlPanel>(processorRef);
    addAndMakeVisible(*controlPanel);

    setResizable(true, true);
    setResizeLimits(480, 420, 2400, 1600);
    setSize(900, 620);
    startTimerHz(4);
}

CliptoneAudioProcessorEditor::~CliptoneAudioProcessorEditor() {
    setLookAndFeel(nullptr);
}

void CliptoneAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(24.0f));
}

void CliptoneAudioProcessorEditor::resized() {
    auto bounds = getLocalBounds();
    controlPanel->setBounds(bounds.removeFromBottom(60));
    tabs.setBounds(bounds);
}

void CliptoneAudioProcessorEditor::openClip(int trackIndex, int clipIndex) {
    static_cast<ClipTab*>(clipTab.get())->showClip(trackIndex, clipIndex);
    tabs.setCurrentTabIndex(3);
}

// (Re)creates every tab — on open, and whenever a different set is loaded (its scene / track
// counts and content may differ).
void CliptoneAudioProcessorEditor::buildTabs() {
    const int current = tabs.getNumTabs() > 0 ? tabs.getCurrentTabIndex() : 1;
    tabs.clearTabs();

    projectTab = std::make_unique<ProjectTab>(processorRef);
    auto session = std::make_unique<SessionTab>(processorRef);
    session->onOpenClip = [this](int trackIndex, int clipIndex) { openClip(trackIndex, clipIndex); };
    sessionTab = std::move(session);
    arrangeTab = std::make_unique<ArrangeTab>(processorRef);
    clipTab = std::make_unique<ClipTab>(processorRef);
    deviceTab = std::make_unique<DeviceTab>(processorRef);
    mixerTab = std::make_unique<MixerTab>(processorRef);
    settingsTab = std::make_unique<SettingsTab>(processorRef);

    const auto tabColour = lookAndFeel.findColour(juce::ResizableWindow::backgroundColourId);
    tabs.addTab("Projects", tabColour, projectTab.get(), false);
    tabs.addTab("Session", tabColour, sessionTab.get(), false);
    tabs.addTab("Arrange", tabColour, arrangeTab.get(), false);
    tabs.addTab("Clip", tabColour, clipTab.get(), false);
    tabs.addTab("Device", tabColour, deviceTab.get(), false);
    tabs.addTab("Mixer", tabColour, mixerTab.get(), false);
    tabs.addTab("Settings", tabColour, settingsTab.get(), false);
    tabs.setCurrentTabIndex(juce::jlimit(0, tabs.getNumTabs() - 1, current));
    shownSetVersion = processorRef.getSetVersion();
}

void CliptoneAudioProcessorEditor::timerCallback() {
    if (processorRef.getSetVersion() != shownSetVersion) {
        buildTabs();
    }
}
