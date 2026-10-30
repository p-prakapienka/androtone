#include "SettingsView.h"

namespace {
    const juce::Colour text { 0xffc5c8c6 };
}

SettingsView::SettingsView(juce::AudioDeviceManager& deviceManagerToUse) :
    deviceManager(deviceManagerToUse),
    // Outputs only; MIDI inputs are opened automatically, so there is no MIDI list here.
    deviceSelector(deviceManagerToUse, 0, 0, 2, 2, false, false, true, false) {
    statusLabel.setFont(juce::FontOptions(16.0f));
    statusLabel.setColour(juce::Label::textColourId, text);
    content.addAndMakeVisible(statusLabel);
    content.addAndMakeVisible(deviceSelector);
    deviceSelector.addComponentListener(this); // it sets its own height as its controls change

    newSongButton.onClick = [this] {
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "New song",
                                           "Start an empty song? The current one is not saved.", "New song", "Cancel",
                                           nullptr, juce::ModalCallbackFunction::create([this](int result) {
                                               if (result != 0 && onNewSong) {
                                                   onNewSong();
                                               }
                                           }));
    };
    demoSongButton.onClick = [this] {
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Demo song",
                                           "Load the demo song? The current one is not saved.", "Load demo", "Cancel",
                                           nullptr, juce::ModalCallbackFunction::create([this](int result) {
                                               if (result != 0 && onDemoSong) {
                                                   onDemoSong();
                                               }
                                           }));
    };
    content.addAndMakeVisible(newSongButton);
    content.addAndMakeVisible(demoSongButton);

    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);
}

SettingsView::~SettingsView() {
    deviceSelector.removeComponentListener(this);
}

void SettingsView::componentMovedOrResized(juce::Component&, bool, bool wasResized) {
    if (wasResized) {
        layoutBelowSelector();
    }
}

void SettingsView::visibilityChanged() {
    // Only measure while the tab is showing.
    if (isShowing()) {
        timerCallback();
        startTimerHz(4);
    } else {
        stopTimer();
    }
}

void SettingsView::timerCallback() {
    juce::String status;
    if (auto* device = deviceManager.getCurrentAudioDevice()) {
        const double rate = device->getCurrentSampleRate();
        const int buffer = device->getCurrentBufferSizeSamples();
        const int latency = buffer + device->getOutputLatencyInSamples();
        status << deviceManager.getCurrentAudioDeviceType() << "  |  " << juce::String(rate / 1000.0, 1) << " kHz, "
               << buffer << " samples  |  latency " << juce::String(1000.0 * latency / juce::jmax(1.0, rate), 1)
               << " ms  |  CPU " << juce::roundToInt(100.0 * deviceManager.getCpuUsage()) << "%";
    } else {
        status = "No audio device";
    }
    statusLabel.setText(status, juce::dontSendNotification);
}

void SettingsView::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1d1f21));
}

void SettingsView::resized() {
    viewport.setBounds(getLocalBounds());
    const int width = juce::jmax(1, viewport.getMaximumVisibleWidth());
    auto area = juce::Rectangle<int>(0, 0, width, 0).reduced(8, 0);
    statusLabel.setBounds(area.getX(), 8, area.getWidth(), 28);
    deviceSelector.setBounds(area.getX(), 44, juce::jmin(area.getWidth(), 640), juce::jmax(200, deviceSelector.getHeight()));
    layoutBelowSelector();
}

void SettingsView::layoutBelowSelector() {
    const int left = deviceSelector.getX();
    const int buttonsTop = deviceSelector.getBottom() + 12;
    newSongButton.setBounds(left, buttonsTop, 140, 40);
    demoSongButton.setBounds(left + 150, buttonsTop, 140, 40);
    content.setSize(juce::jmax(1, viewport.getMaximumVisibleWidth()), buttonsTop + 52);
}
