#pragma once

#include "RackView.h"
#include "core/Engine.h"
#include "core/Session.h"

#include <juce_audio_utils/juce_audio_utils.h>

// Transport bar, song save/load, and the rack. The editors, song view, and mixer screens are
// added as the roadmap reaches them.
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
    Engine engine;
    Session session { engine };

    juce::Label title;
    juce::TextButton playButton { "Play" };
    juce::TextButton saveButton { "Save" };
    juce::TextButton loadButton { "Load" };
    juce::Slider tempoSlider;
    juce::Label positionLabel;
    RackView rack { session, engine };
    std::unique_ptr<juce::FileChooser> chooser;

    void timerCallback() override;
    void refreshFromSong();
    void saveSong();
    void loadSong();
    static void showError(const juce::String& message);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
