#include "MainComponent.h"

#include "NatronLookAndFeel.h"
#include "Theme.h"
#include "core/Autosave.h"
#include "core/DemoSong.h"
#include "core/SongExport.h"
#include "core/effects/Effect.h"

namespace {
    const juce::Colour background = NatronLookAndFeel::background;

    std::unique_ptr<juce::OutputStream> openForWriting(const juce::URL& url) {
        if (url.isLocalFile()) {
            auto file = std::make_unique<juce::FileOutputStream>(url.getLocalFile());
            if (!file->openedOk()) {
                return nullptr;
            }
            file->setPosition(0); // FileOutputStream appends by default
            file->truncate();
            return file;
        }
        return url.createOutputStream(); // Android content:// URIs
    }
}

// Renders on a background thread (with its own engine, see SongExport), then writes the WAV.
class MainComponent::ExportJob : public juce::ThreadWithProgressWindow {
public:
    ExportJob(Song songToRender, SongExport::Options optionsToUse, juce::URL target) :
        ThreadWithProgressWindow("Exporting...", true, true),
        song(std::move(songToRender)), options(optionsToUse), url(std::move(target)) {}

    void run() override {
        juce::AudioBuffer<float> audio;
        result = SongExport::render(song, options, audio, [this](double done) {
            setProgress(done * 0.95);
            return !threadShouldExit();
        });
        if (result.wasOk()) {
            setStatusMessage("Writing WAV...");
            auto stream = openForWriting(url);
            result = stream != nullptr ? SongExport::writeWav(audio, options.sampleRate, options.bitDepth, *stream)
                                       : juce::Result::fail("Could not open the file for writing");
        }
    }

    void threadComplete(bool userPressedCancel) override {
        if (!userPressedCancel && result.failed()) {
            MainComponent::showError(result.getErrorMessage());
        }
    }

private:
    Song song;
    SongExport::Options options;
    juce::URL url;
    juce::Result result = juce::Result::ok();
};

MainComponent::MainComponent() {
    // Pick up where the last run left off; the demo song on first launch (or a damaged autosave).
    if (Autosave::restore(session, Autosave::defaultFile()).failed()) {
        const auto loaded = session.loadSong(makeDemoSong());
        jassert(loaded.wasOk());
        juce::ignoreUnused(loaded);
    }

    juce::PropertiesFile::Options options;
    options.applicationName = "Natron";
    options.filenameSuffix = ".settings";
    options.folderName = "Natron";
    options.osxLibrarySubFolder = "Application Support";
    settings.setStorageParameters(options);
    const bool metronomeOn = settings.getUserSettings()->getBoolValue("metronome", false);
    engine.setMetronome(metronomeOn);
    clickButton.setToggleState(metronomeOn, juce::dontSendNotification);

    title.setText("Natron", juce::dontSendNotification);
    title.setFont(NatronLookAndFeel::labelFont(24.0f));
    title.setColour(juce::Label::textColourId, juce::Colour(0xffe8eaed));
    addAndMakeVisible(title);

    playButton.setClickingTogglesState(true);
    playButton.onClick = [this] { session.setPlaying(playButton.getToggleState()); };
    addAndMakeVisible(playButton);

    modeButton.onClick = [this] {
        session.setSongMode(!session.isSongMode());
        refreshFromSong();
    };
    addAndMakeVisible(modeButton);

    saveButton.onClick = [this] { saveSong(); };
    addAndMakeVisible(saveButton);
    loadButton.onClick = [this] { loadSong(); };
    addAndMakeVisible(loadButton);
    exportButton.onClick = [this] { exportAudio(exportButton); };
    addAndMakeVisible(exportButton);
    fileButton.onClick = [this] { showFileMenu(); };
    addChildComponent(fileButton);
    recordButton.setClickingTogglesState(true);
    recordButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffcc3333));
    recordButton.onClick = [this] { session.setRecording(recordButton.getToggleState()); };
    addAndMakeVisible(recordButton);
    clickButton.setClickingTogglesState(true);
    clickButton.onClick = [this] {
        engine.setMetronome(clickButton.getToggleState());
        settings.getUserSettings()->setValue("metronome", clickButton.getToggleState());
    };
    addAndMakeVisible(clickButton);
    tapButton.setTriggeredOnMouseDown(true); // the press is the beat
    tapButton.onClick = [this] {
        if (const auto bpm = tapTempo.tap(juce::Time::getMillisecondCounterHiRes() / 1000.0)) {
            session.setTempo(std::round(*bpm));
            tempoSlider.setValue(session.getSong().tempo, juce::dontSendNotification);
        }
    };
    addAndMakeVisible(tapButton);
    undoButton.onClick = [this] { undo(); };
    addAndMakeVisible(undoButton);
    redoButton.onClick = [this] { redo(); };
    addAndMakeVisible(redoButton);
    setWantsKeyboardFocus(true);

    tempoSlider.setRange(40.0, 240.0, 1.0);
    tempoSlider.setTextValueSuffix(" BPM");
    tempoSlider.setSliderStyle(juce::Slider::LinearBar); // drag across the box; the value shows inside
    tempoSlider.onValueChange = [this] { session.setTempo(tempoSlider.getValue()); };
    addAndMakeVisible(tempoSlider);

    positionLabel.setJustificationType(juce::Justification::centred);
    positionLabel.setFont(juce::FontOptions(20.0f));
    positionLabel.setColour(juce::Label::textColourId, NatronLookAndFeel::lcdText);
    positionLabel.setColour(juce::Label::backgroundColourId, NatronLookAndFeel::lcd);
    addAndMakeVisible(positionLabel);

    rackPage = new RackPage(*this);
    machinePage = new MachinePage(*this);
    patternEditor = new PatternEditorView(session, engine, keyboardState);
    keyboardState.addListener(this);
    songView = new SongView(session, engine);
    mixerView = new MixerView(session);
    settingsView = new SettingsView(deviceManager);
    settingsView->onNewSong = [this] { loadNewSong(Song {}); };
    settingsView->onDemoSong = [this] { loadNewSong(makeDemoSong()); };
    const juce::Colour tabColour = NatronLookAndFeel::background;
    tabs.addTab("Rack", tabColour, rackPage, true);
    tabs.addTab("Machine", tabColour, machinePage, true);
    tabs.addTab("Pattern", tabColour, patternEditor, true);
    tabs.addTab("Song", tabColour, songView, true);
    tabs.addTab("Mixer", tabColour, mixerView, true);
    tabs.addTab("Settings", tabColour, settingsView, true);
    tabs.setTabBarDepth(40);
    addAndMakeVisible(tabs);
    refreshFromSong();

    setSize(1100, 640);
    // The last audio device and buffer size; on Android the default device type is Oboe (AAudio).
    const auto savedAudio = settings.getUserSettings()->getXmlValue("audioDevice");
    setAudioChannels(0, 2, savedAudio.get());
    deviceManager.addChangeListener(this);
    startTimerHz(30);
}

MainComponent::~MainComponent() {
    deviceManager.removeChangeListener(this);
    keyboardState.removeListener(this);
    for (const auto& identifier : openMidiInputs) {
        deviceManager.removeMidiInputDeviceCallback(identifier, this);
    }
    shutdownAudio();
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster*) {
    // The audio device or buffer size changed (Settings tab): remember it for the next launch.
    if (const auto state = deviceManager.createStateXml()) {
        settings.getUserSettings()->setValue("audioDevice", state.get());
        settings.saveIfNeeded();
    }
}

void MainComponent::saveAutosave() {
    const auto result = Autosave::save(session, Autosave::defaultFile());
    if (result.failed()) {
        DBG("Autosave failed: " << result.getErrorMessage());
    }
}

bool MainComponent::handleBackButton() {
    if (tabs.getCurrentTabIndex() != 0) {
        tabs.setCurrentTabIndex(0);
        return true;
    }
    return false;
}

void MainComponent::undo() {
    if (session.undo()) {
        refreshFromSong();
    }
}

void MainComponent::redo() {
    if (session.redo()) {
        refreshFromSong();
    }
}

void MainComponent::updateUndoButtons() {
    undoButton.setEnabled(session.canUndo());
    redoButton.setEnabled(session.canRedo());
}

bool MainComponent::keyPressed(const juce::KeyPress& key) {
    // Ctrl/Cmd+Z undo; Ctrl/Cmd+Shift+Z or Ctrl/Cmd+Y redo.
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() && key.getKeyCode() == 'Z') {
        mods.isShiftDown() ? redo() : undo();
        return true;
    }
    if (mods.isCommandDown() && key.getKeyCode() == 'Y') {
        redo();
        return true;
    }
    return false;
}

void MainComponent::loadNewSong(const Song& song) {
    session.setPlaying(false);
    const auto result = session.loadSong(song);
    if (result.failed()) {
        showError(result.getErrorMessage());
    }
    refreshFromSong();
}

void MainComponent::showFileMenu() {
    juce::PopupMenu menu;
    menu.addItem("Save...", [this] { saveSong(); });
    menu.addItem("Load...", [this] { loadSong(); });
    menu.addItem("Export...", [this] { exportAudio(fileButton); });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&fileButton));
}

void MainComponent::handleNoteOn(juce::MidiKeyboardState*, int channel, int note, float velocity) {
    engine.pushLiveMidi(juce::MidiMessage::noteOn(channel, note, velocity));
}

void MainComponent::handleNoteOff(juce::MidiKeyboardState*, int channel, int note, float) {
    engine.pushLiveMidi(juce::MidiMessage::noteOff(channel, note));
}

void MainComponent::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) {
    engine.pushLiveMidi(message); // MIDI device thread: the engine's queue is safe for this
}

void MainComponent::scanMidiInputs() {
    // Open every MIDI input (USB, Bluetooth, virtual); new devices are picked up as they appear.
    for (const auto& device : juce::MidiInput::getAvailableDevices()) {
        if (!openMidiInputs.contains(device.identifier)) {
            deviceManager.setMidiInputDeviceEnabled(device.identifier, true);
            deviceManager.addMidiInputDeviceCallback(device.identifier, this);
            openMidiInputs.add(device.identifier);
        }
    }
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    engine.prepare(sampleRate, samplesPerBlockExpected);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
    auto* const* channels = bufferToFill.buffer->getArrayOfWritePointers();
    juce::AudioBuffer<float> region(channels, bufferToFill.buffer->getNumChannels(), bufferToFill.startSample,
                                    bufferToFill.numSamples);
    engine.process(region);
}

void MainComponent::releaseResources() {}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(background);
}

void MainComponent::resized() {
    // When the full bar doesn't fit (phones in landscape), drop the title and fold Save / Load /
    // Export into a File menu. The full bar needs ~1100 px with a usable tempo control.
    const bool narrow = getWidth() < 1100;
    auto area = getLocalBounds().reduced(narrow ? 6 : 12);
    auto bar = area.removeFromTop(44);
    title.setVisible(!narrow);
    if (!narrow) {
        title.setBounds(bar.removeFromLeft(110));
    }
    playButton.setBounds(bar.removeFromLeft(84).reduced(3, 0));
    recordButton.setBounds(bar.removeFromLeft(60).reduced(3, 0));
    modeButton.setBounds(bar.removeFromLeft(84).reduced(3, 0));
    positionLabel.setBounds(bar.removeFromLeft(60));
    undoButton.setBounds(bar.removeFromLeft(narrow ? 60 : 70).reduced(3, 0));
    redoButton.setBounds(bar.removeFromLeft(narrow ? 60 : 70).reduced(3, 0));
    for (auto* button : { &saveButton, &loadButton, &exportButton }) {
        button->setVisible(!narrow);
    }
    fileButton.setVisible(narrow);
    if (narrow) {
        fileButton.setBounds(bar.removeFromRight(76).reduced(3, 0));
    } else {
        exportButton.setBounds(bar.removeFromRight(84).reduced(3, 0));
        loadButton.setBounds(bar.removeFromRight(76).reduced(3, 0));
        saveButton.setBounds(bar.removeFromRight(76).reduced(3, 0));
    }
    tapButton.setBounds(bar.removeFromRight(56).reduced(3, 0));
    clickButton.setBounds(bar.removeFromRight(60).reduced(3, 0));
    tempoSlider.setBounds(bar.reduced(6, 2).withSizeKeepingCentre(juce::jmin(bar.getWidth() - 12, 220), bar.getHeight() - 4));
    area.removeFromTop(narrow ? 6 : 10);
    tabs.setBounds(area);
}

void MainComponent::selectMachine(int machine) {
    engine.setLiveTarget(machine); // the keyboard and MIDI devices play the selected machine
    rackPage->rack.setSelectedMachine(machine);
    rackPage->resized();
    machinePage->showMachine(machine);
    patternEditor->setMachine(machine);
}

void MainComponent::refreshFromSong() {
    tempoSlider.setValue(session.getSong().tempo, juce::dontSendNotification);
    playButton.setToggleState(session.isPlaying(), juce::dontSendNotification);
    playButton.setButtonText(session.isPlaying() ? "Stop" : "Play");
    modeButton.setButtonText(session.isSongMode() ? "Song" : "Pattern");
    recordButton.setToggleState(session.isRecording(), juce::dontSendNotification);
    selectMachine(juce::jlimit(0, juce::jmax(0, session.getNumMachines() - 1), rackPage->rack.getSelectedMachine()));
    songView->refresh();
    mixerView->rebuild();
    resized();
}

void MainComponent::timerCallback() {
    session.collectGarbage();
    session.pollRecording();
    if (session.isInGesture() && !session.isRecording() && juce::Desktop::getInstance().getNumDraggingMouseSources() == 0) {
        session.cancelGestures(); // a press whose release went to a component that was deleted
    }
    updateUndoButtons();
    if (--midiScanCountdown <= 0) {
        scanMidiInputs();
        midiScanCountdown = 60; // every two seconds
    }

    // Song mode stops by itself at the end of the song.
    if (playButton.getToggleState() != session.isPlaying()) {
        playButton.setToggleState(session.isPlaying(), juce::dontSendNotification);
    }
    playButton.setButtonText(session.isPlaying() ? "Stop" : "Play");
    const double beat = session.isPlaying() ? engine.getPlayheadBeat() : 0.0;
    const int barNumber = static_cast<int>(beat / Transport::beatsPerBar) + 1;
    const int beatInBar = static_cast<int>(std::fmod(beat, Transport::beatsPerBar)) + 1;
    positionLabel.setText(juce::String(barNumber) + "." + juce::String(beatInBar), juce::dontSendNotification);
    rackPage->rack.repaint();
}

MainComponent::RackPage::RackPage(MainComponent& owner) :
    rack(owner.session, owner.engine) {
    rack.onSelect = [&owner](int machine) { owner.selectMachine(machine); };
    rack.onRackChanged = [&owner] { owner.refreshFromSong(); };
    viewport.setViewedComponent(&rack, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);
}

void MainComponent::RackPage::resized() {
    viewport.setBounds(getLocalBounds());
    rack.setBounds(0, 6, juce::jmax(1, viewport.getMaximumVisibleWidth()), rack.getIdealHeight());
}

MainComponent::MachinePage::MachinePage(MainComponent& ownerToUse) :
    owner(ownerToUse) {
    for (int slot = 0; slot < Engine::effectsPerMachine; slot++) {
        auto& controls = effectSlots[static_cast<size_t>(slot)];
        controls.label.setText("FX " + juce::String(slot + 1), juce::dontSendNotification);
        controls.label.setFont(NatronLookAndFeel::labelFont(14.0f));
        controls.label.setColour(juce::Label::textColourId, juce::Colour(0xffd5d8dc));
        controls.type.addItem("None", 1);
        controls.type.addItemList(EffectRegistry::getTypeNames(), 2);
        controls.type.onChange = [this, slot] {
            const auto& box = effectSlots[static_cast<size_t>(slot)].type;
            owner.session.setEffect(machine, slot, box.getSelectedId() == 1 ? juce::String() : box.getText());
            showMachine(machine);
        };
        content.addAndMakeVisible(controls.label);
        content.addAndMakeVisible(controls.type);
    }

    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(false, true); // a strip: swipe sideways
    addAndMakeVisible(viewport);
}

void MainComponent::MachinePage::showMachine(int machineIndex) {
    machine = machineIndex;
    machinePanel.reset();
    extras.reset();
    for (auto& controls : effectSlots) {
        controls.panel.reset();
    }

    const bool valid = machine >= 0 && machine < owner.session.getNumMachines();
    if (valid) {
        const auto& state = owner.session.getSong().machines[static_cast<size_t>(machine)];
        machinePanel = std::make_unique<ParameterPanel>(
            *owner.session.getParameters(machine),
            [this](const juce::String& id, float value) { owner.session.setParameter(machine, id, value); },
            state.name + " (" + state.type + ")");
        machinePanel->setAccentColour(Theme::machineColour(state.type));
        machinePanel->setLayout(state.type, Theme::panelColour(state.type));
        machinePanel->setFitToHeight(true);
        content.addAndMakeVisible(*machinePanel);
        if (MachineExtrasPanel::hasExtras(state.type)) {
            extras = std::make_unique<MachineExtrasPanel>(owner.session, machine, [](const juce::String& message) { showError(message); });
            content.addAndMakeVisible(*extras);
        }

        for (int slot = 0; slot < Engine::effectsPerMachine; slot++) {
            auto& controls = effectSlots[static_cast<size_t>(slot)];
            const auto& type = state.effects[static_cast<size_t>(slot)].type;
            controls.type.setText(type.isEmpty() ? "None" : type, juce::dontSendNotification);
            if (const auto* parameters = owner.session.getEffectParameters(machine, slot)) {
                controls.panel = std::make_unique<ParameterPanel>(
                    *parameters, [this, slot](const juce::String& id, float value) {
                        owner.session.setEffectParameter(machine, slot, id, value);
                    });
                controls.panel->setLayout(type, Theme::effectColour(type)); // a pedal in the effect's colour
                controls.panel->setFitToHeight(true);
                content.addAndMakeVisible(*controls.panel);
            }
        }
    }
    for (auto& controls : effectSlots) {
        controls.label.setVisible(valid);
        controls.type.setVisible(valid);
    }
    resized();
}

void MainComponent::MachinePage::paint(juce::Graphics& g) {
    g.fillAll(NatronLookAndFeel::background);
    if (machinePanel == nullptr) {
        g.setColour(juce::Colour(0xffb9bdc3));
        g.setFont(NatronLookAndFeel::labelFont(15.0f));
        g.drawText("ADD A MACHINE ON THE RACK TAB", getLocalBounds(), juce::Justification::centred);
    }
}

void MainComponent::MachinePage::resized() {
    viewport.setBounds(getLocalBounds());
    // Everything is as tall as the page (less the sideways scroll bar); widths follow.
    const int height = juce::jmax(160, viewport.getHeight() - viewport.getScrollBarThickness() - 2);
    constexpr int gap = 8;
    constexpr int headerHeight = 36;
    int x = 0;
    if (machinePanel != nullptr) {
        machinePanel->setBounds(x, 0, machinePanel->getWidthForHeight(height), height);
        x = machinePanel->getRight() + gap;
    }
    if (extras != nullptr) {
        extras->setBounds(x, 0, 380, juce::jmin(height, extras->getIdealHeight() * 2));
        x = extras->getRight() + gap;
    }
    for (auto& controls : effectSlots) {
        if (!controls.type.isVisible()) {
            continue;
        }
        controls.label.setBounds(x, 2, 56, headerHeight - 6);
        const int pedalHeight = height - headerHeight;
        const int width = controls.panel != nullptr ? juce::jmax(220, controls.panel->getWidthForHeight(pedalHeight)) : 220;
        controls.type.setBounds(x + 58, 2, width - 58, headerHeight - 6);
        if (controls.panel != nullptr) {
            controls.panel->setBounds(x, headerHeight, width, pedalHeight);
        }
        x += width + gap;
    }
    content.setSize(juce::jmax(x, viewport.getWidth()), height);
}

void MainComponent::saveSong() {
    chooser = std::make_unique<juce::FileChooser>(
        "Save song", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("song.natron"),
        "*.natron");
    const auto chooserFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting;

    chooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fileChooser) {
        const auto url = fileChooser.getURLResult();
        if (url.isEmpty()) {
            return;
        }
        const auto stream = openForWriting(url);
        const auto result = stream != nullptr ? session.saveToStream(*stream)
                                              : juce::Result::fail("Could not open the file for writing");
        if (result.failed()) {
            showError(result.getErrorMessage());
        }
    });
}

void MainComponent::loadSong() {
    chooser = std::make_unique<juce::FileChooser>(
        "Load song", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.natron");
    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fileChooser) {
        const auto url = fileChooser.getURLResult();
        if (url.isEmpty()) {
            return;
        }
        const auto stream = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress));
        const auto result = stream != nullptr ? session.loadFromStream(*stream)
                                              : juce::Result::fail("Could not open the file");
        if (result.failed()) {
            showError(result.getErrorMessage());
        }
        refreshFromSong();
    });
}

void MainComponent::exportAudio(juce::Component& anchor) {
    juce::PopupMenu menu;
    const bool hasSong = session.getSong().endBar() > 0;
    menu.addItem(1, "Export song (timeline) as WAV", hasSong);
    menu.addItem(2, "Export pattern loop (4 bars) as WAV");
    menu.addSeparator();
    menu.addItem(3, "Export song (timeline) as MIDI", hasSong);
    menu.addItem(4, "Export pattern loop (4 bars) as MIDI");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&anchor), [this](int result) {
        if (result == 1 || result == 2) {
            startExport(result == 1);
        } else if (result == 3 || result == 4) {
            startMidiExport(result == 3);
        }
    });
}

void MainComponent::startMidiExport(bool wholeSong) {
    chooser = std::make_unique<juce::FileChooser>(
        "Export MIDI", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("natron.mid"), "*.mid");
    const auto chooserFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting;

    chooser->launchAsync(chooserFlags, [this, wholeSong](const juce::FileChooser& fileChooser) {
        const auto url = fileChooser.getURLResult();
        if (url.isEmpty()) {
            return;
        }
        SongExport::Options options;
        options.range = wholeSong ? SongExport::Options::Range::song : SongExport::Options::Range::pattern;
        options.patternBars = 4;
        auto stream = openForWriting(url);
        const auto result = stream != nullptr ? SongExport::writeMidi(session.getSong(), options, *stream)
                                              : juce::Result::fail("Could not open the file for writing");
        if (result.failed()) {
            showError(result.getErrorMessage());
        }
    });
}

void MainComponent::startExport(bool wholeSong) {
    chooser = std::make_unique<juce::FileChooser>(
        "Export WAV", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("natron.wav"), "*.wav");
    const auto chooserFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting;

    chooser->launchAsync(chooserFlags, [this, wholeSong](const juce::FileChooser& fileChooser) {
        const auto url = fileChooser.getURLResult();
        if (url.isEmpty()) {
            return;
        }
        SongExport::Options options;
        options.range = wholeSong ? SongExport::Options::Range::song : SongExport::Options::Range::pattern;
        options.patternBars = 4;
        exportJob = std::make_unique<ExportJob>(session.getSong(), options, url);
        exportJob->launchThread();
    });
}

void MainComponent::showError(const juce::String& message) {
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Natron", message);
}
