#pragma once

#include "RackScroll.h"
#include "SettingsView.h"
#include "SongView.h"
#include "core/Engine.h"
#include "core/Session.h"
#include "core/TapTempo.h"

#include <juce_audio_utils/juce_audio_utils.h>

// One page above a transport bar along the bottom: the rack — every screen in one vertical scroll
// (see RackScroll): each machine's sound editor and pattern editor, the FX screens, the mixer
// screens, the master and the song timeline — or settings (from the menu).
// Bottom bar: Song, Menu (undo/redo, tempo, files, export, settings), the display (song name and
// position; tap it for tempo), Save, Pattern/Song mode, Click, Play, Stop, Rec.
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

    // Android back button: returns to the rack; returns false (leave the app) when already there.
    bool handleBackButton();

private:
    class ExportJob;
    class Display;
    class TempoPanel;

    Engine engine;
    Session session { engine };
    juce::MidiKeyboardState keyboardState;
    juce::StringArray openMidiInputs;
    int midiScanCountdown = 0;

    juce::TextButton songButton { "Song" };   // jumps to the timeline (the rack's last screen) and back
    juce::TextButton menuButton { "Menu" };
    std::unique_ptr<Display> display;          // song name and position; tap for tempo
    juce::TextButton saveButton { "Save" };
    juce::TextButton modeButton { "Pattern" };
    juce::TextButton clickButton { "Click" };  // metronome
    juce::TextButton playButton { "Play" };
    juce::TextButton stopButton { "Stop" };
    juce::TextButton recordButton { "Rec" };   // record live notes into the playing pattern
    TapTempo tapTempo;
    juce::String songName { "Untitled" };
    juce::File songFile;                       // where Save writes, once saved or loaded

    std::unique_ptr<RackScroll> rack;
    std::unique_ptr<SettingsView> settingsView;
    juce::Component* page = nullptr;           // the one showing
    int selectedMachine = 0;
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
    void showMenu();
    void showTempo();
    void showPage(juce::Component* newPage);
    void loadNewSong(const Song& song);
    void undo();
    void redo();
    void startExport(bool wholeSong);
    void startMidiExport(bool wholeSong);
    static void showError(const juce::String& message);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
