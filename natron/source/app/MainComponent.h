#pragma once

#include "MachineExtrasPanel.h"
#include "MixerView.h"
#include "ParameterPanel.h"
#include "PatternEditorView.h"
#include "RackView.h"
#include "SettingsView.h"
#include "SongView.h"
#include "core/Engine.h"
#include "core/Session.h"
#include "core/TapTempo.h"

#include <juce_audio_utils/juce_audio_utils.h>

// Transport bar (play, mode, tempo, position, save/load/export) over five tabs:
//  Rack     — machines and pattern pads, the selected machine's controls and its two effect slots
//  Pattern  — piano roll / step grid for the selected machine's selected pattern
//  Song     — the song timeline
//  Mixer    — channel strips and the master section
//  Settings — audio device and buffer size, CPU, new/demo song
// The song is autosaved when the app is backgrounded or closed, and restored on launch.
class MainComponent : public juce::AudioAppComponent,
                      private juce::Timer,
                      private juce::ChangeListener,
                      private juce::MidiKeyboardState::Listener,
                      private juce::MidiInputCallback {
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    // Writes the autosave; called when the app is suspended or quits.
    void saveAutosave();

    // Android back button: returns to the Rack tab; returns false (leave the app) when already there.
    bool handleBackButton();

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
        std::unique_ptr<MachineExtrasPanel> extras;
        std::array<EffectSlotControls, Engine::effectsPerMachine> effectSlots;
    };

    class ExportJob;

    Engine engine;
    Session session { engine };
    juce::MidiKeyboardState keyboardState;
    juce::StringArray openMidiInputs;
    int midiScanCountdown = 0;

    juce::Label title;
    juce::TextButton playButton { "Play" };
    juce::TextButton modeButton { "Pattern" };
    juce::TextButton saveButton { "Save" };
    juce::TextButton loadButton { "Load" };
    juce::TextButton exportButton { "Export" };
    juce::TextButton fileButton { "File" }; // replaces Save/Load/Export on narrow screens
    juce::TextButton recordButton { "Rec" }; // record live notes into the playing pattern
    juce::TextButton clickButton { "Click" }; // metronome
    juce::TextButton tapButton { "Tap" };     // tap tempo
    TapTempo tapTempo;
    juce::TextButton undoButton { "Undo" };
    juce::TextButton redoButton { "Redo" };
    juce::Slider tempoSlider;
    juce::Label positionLabel;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    RackPage* rackPage = nullptr;
    PatternEditorView* patternEditor = nullptr;
    SongView* songView = nullptr;
    MixerView* mixerView = nullptr;
    SettingsView* settingsView = nullptr;
    juce::ApplicationProperties settings;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<ExportJob> exportJob;

    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void handleNoteOn(juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void handleNoteOff(juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) override;
    void scanMidiInputs();
    void refreshFromSong();
    void selectMachine(int machine);
    void saveSong();
    void loadSong();
    void exportAudio(juce::Component& anchor);
    void showFileMenu();
    void loadNewSong(const Song& song);
    void undo();
    void redo();
    void updateUndoButtons();
    void startExport(bool wholeSong);
    static void showError(const juce::String& message);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
