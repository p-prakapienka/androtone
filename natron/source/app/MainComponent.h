#pragma once

#include "MixerView.h"
#include "ParameterPanel.h"
#include "PatternEditorView.h"
#include "RackView.h"
#include "SongView.h"
#include "core/Engine.h"
#include "core/Session.h"

#include <juce_audio_utils/juce_audio_utils.h>

// Transport bar (play, mode, tempo, position, save/load/export) over four tabs:
//  Rack    — machines and pattern pads, the selected machine's controls and its two effect slots
//  Pattern — piano roll / step grid for the selected machine's selected pattern
//  Song    — the song timeline
//  Mixer   — channel strips and the master section
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
    // The Rack tab: the rack on top; below, the selected machine's controls and effect slots.
    class RackPage : public juce::Component {
    public:
        explicit RackPage(MainComponent& owner);
        void resized() override;
        void showMachine(int machine);

        RackView rack;

    private:
        struct EffectSlotControls {
            juce::Label label;
            juce::ComboBox type;
            std::unique_ptr<ParameterPanel> panel;
        };

        MainComponent& owner;
        juce::Viewport viewport;
        juce::Component content;
        std::unique_ptr<ParameterPanel> machinePanel;
        std::array<EffectSlotControls, Engine::effectsPerMachine> effectSlots;
    };

    class ExportJob;

    Engine engine;
    Session session { engine };

    juce::Label title;
    juce::TextButton playButton { "Play" };
    juce::TextButton modeButton { "Pattern" };
    juce::TextButton saveButton { "Save" };
    juce::TextButton loadButton { "Load" };
    juce::TextButton exportButton { "Export" };
    juce::Slider tempoSlider;
    juce::Label positionLabel;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    RackPage* rackPage = nullptr;
    PatternEditorView* patternEditor = nullptr;
    SongView* songView = nullptr;
    MixerView* mixerView = nullptr;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<ExportJob> exportJob;

    void timerCallback() override;
    void refreshFromSong();
    void selectMachine(int machine);
    void saveSong();
    void loadSong();
    void exportAudio();
    void startExport(bool wholeSong);
    static void showError(const juce::String& message);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
