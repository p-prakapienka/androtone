#pragma once

#include "MixerView.h"
#include "ParameterPanel.h"
#include "RackView.h"
#include "core/Engine.h"
#include "core/Session.h"

#include <juce_audio_utils/juce_audio_utils.h>

// Transport bar, song save/load, and two tabs: Rack (machines, pattern pads, and the selected
// machine's controls) and Mixer (channel strips and the master section).
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
    // The Rack tab: the rack on top, the selected machine's controls below, scrolling together.
    class RackPage : public juce::Component {
    public:
        explicit RackPage(MainComponent& owner);
        void resized() override;
        void showMachine(int machine);

        RackView rack;

    private:
        MainComponent& owner;
        juce::Viewport viewport;
        juce::Component content;
        std::unique_ptr<ParameterPanel> machinePanel;
    };

    Engine engine;
    Session session { engine };

    juce::Label title;
    juce::TextButton playButton { "Play" };
    juce::TextButton saveButton { "Save" };
    juce::TextButton loadButton { "Load" };
    juce::Slider tempoSlider;
    juce::Label positionLabel;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    RackPage* rackPage = nullptr;
    MixerView* mixerView = nullptr;
    std::unique_ptr<juce::FileChooser> chooser;

    void timerCallback() override;
    void refreshFromSong();
    void saveSong();
    void loadSong();
    static void showError(const juce::String& message);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
