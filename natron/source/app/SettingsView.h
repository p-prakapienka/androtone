#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>

// The Settings tab: audio output device and buffer size (smaller = lower latency, more CPU risk),
// a live CPU and latency readout, and starting a new or the demo song.
class SettingsView : public juce::Component, private juce::Timer, private juce::ComponentListener {
public:
    explicit SettingsView(juce::AudioDeviceManager& deviceManagerToUse);
    ~SettingsView() override;

    std::function<void()> onNewSong;
    std::function<void()> onDemoSong;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    juce::AudioDeviceManager& deviceManager;
    juce::AudioDeviceSelectorComponent deviceSelector;
    juce::Label statusLabel;
    juce::TextButton newSongButton { "New song" };
    juce::TextButton demoSongButton { "Demo song" };
    juce::Viewport viewport;
    juce::Component content;

    void visibilityChanged() override;
    void componentMovedOrResized(juce::Component&, bool, bool wasResized) override;
    void layoutBelowSelector();
    void timerCallback() override;
};
