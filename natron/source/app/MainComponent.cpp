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

// Tempo: a bar to drag and a key to tap, in a call-out from the display.
class MainComponent::TempoPanel : public juce::Component {
public:
    explicit TempoPanel(MainComponent& ownerToUse) : owner(ownerToUse) {
        tempo.setSliderStyle(juce::Slider::LinearBar);
        tempo.setRange(40.0, 240.0, 1.0);
        tempo.setTextValueSuffix(" BPM");
        tempo.setValue(owner.session.getSong().tempo, juce::dontSendNotification);
        tempo.onValueChange = [this] { owner.session.setTempo(tempo.getValue()); };
        addAndMakeVisible(tempo);
        tap.setTriggeredOnMouseDown(true); // the press is the beat
        tap.onClick = [this] {
            if (const auto bpm = owner.tapTempo.tap(juce::Time::getMillisecondCounterHiRes() / 1000.0)) {
                owner.session.setTempo(std::round(*bpm));
                tempo.setValue(owner.session.getSong().tempo, juce::dontSendNotification);
            }
        };
        addAndMakeVisible(tap);
        setSize(300, 56);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(6);
        tap.setBounds(area.removeFromRight(80));
        area.removeFromRight(6);
        tempo.setBounds(area);
    }

private:
    MainComponent& owner;
    juce::Slider tempo;
    juce::TextButton tap { "Tap" };
};

// The display in the bottom bar: song name over the position (bar.beat.step) and tempo.
class MainComponent::Display : public juce::Component {
public:
    explicit Display(MainComponent& ownerToUse) : owner(ownerToUse) {}

    void paint(juce::Graphics& g) override {
        const auto area = getLocalBounds().toFloat().reduced(1.0f, 1.5f);
        g.setColour(NatronLookAndFeel::lcd);
        g.fillRoundedRectangle(area, 6.0f);
        g.setColour(NatronLookAndFeel::line);
        g.drawRoundedRectangle(area.reduced(0.5f), 6.0f, 1.0f);

        const double beat = owner.session.isPlaying() ? owner.engine.getPlayheadBeat() : 0.0;
        const int bar = static_cast<int>(beat / Transport::beatsPerBar) + 1;
        const int beatInBar = static_cast<int>(std::fmod(beat, Transport::beatsPerBar)) + 1;
        const int step = static_cast<int>(std::fmod(beat, 1.0) * 4.0) + 1;
        auto text = area.reduced(8.0f, 3.0f);
        g.setColour(NatronLookAndFeel::textDim);
        g.setFont(NatronLookAndFeel::labelFont(text.getHeight() * 0.32f));
        g.drawText(owner.songName.toUpperCase(), text.removeFromTop(text.getHeight() * 0.4f), juce::Justification::centredLeft);
        g.setColour(NatronLookAndFeel::lcdText);
        g.setFont(NatronLookAndFeel::displayFont(text.getHeight() * 0.95f));
        g.drawText(juce::String(bar) + "." + juce::String(beatInBar) + "." + juce::String(step), text, juce::Justification::centredLeft);
        g.setColour(NatronLookAndFeel::lcdText.withAlpha(0.7f));
        g.setFont(NatronLookAndFeel::displayFont(text.getHeight() * 0.6f));
        g.drawText(juce::String(juce::roundToInt(owner.session.getSong().tempo)) + " BPM", text, juce::Justification::centredRight);
    }

    void mouseUp(const juce::MouseEvent& event) override {
        if (event.mouseWasClicked()) {
            owner.showTempo();
        }
    }

private:
    MainComponent& owner;
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

    display = std::make_unique<Display>(*this);
    addAndMakeVisible(*display);

    songButton.onClick = [this] {
        showPage(rack.get());
        rack->toggleTimeline();
    };
    menuButton.onClick = [this] { showMenu(); };
    saveButton.onClick = [this] { saveSong(); };
    modeButton.onClick = [this] {
        session.setSongMode(!session.isSongMode());
        refreshFromSong();
    };
    clickButton.setClickingTogglesState(true);
    clickButton.onClick = [this] {
        engine.setMetronome(clickButton.getToggleState());
        settings.getUserSettings()->setValue("metronome", clickButton.getToggleState());
    };
    playButton.setClickingTogglesState(true);
    playButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff57c27c));
    playButton.onClick = [this] { session.setPlaying(true); };
    stopButton.onClick = [this] { session.setPlaying(false); };
    recordButton.setClickingTogglesState(true);
    recordButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffe5484d));
    recordButton.iconColour = juce::Colour(0xffe5484d);
    recordButton.onClick = [this] { session.setRecording(recordButton.getToggleState()); };
    for (juce::Button* button : std::initializer_list<juce::Button*> { &songButton, &menuButton, &saveButton, &modeButton,
                                                                       &clickButton, &playButton, &stopButton, &recordButton }) {
        addAndMakeVisible(*button);
    }
    setWantsKeyboardFocus(true);

    rack = std::make_unique<RackScroll>(session, engine, keyboardState, [](const juce::String& message) { showError(message); });
    rack->onMachineInView = [this](int machine) { selectMachine(machine); };
    rack->onRackChanged = [this] { refreshFromSong(); };
    keyboardState.addListener(this);
    settingsView = std::make_unique<SettingsView>(deviceManager);
    settingsView->onNewSong = [this] { loadNewSong(Song {}); };
    settingsView->onDemoSong = [this] { loadNewSong(makeDemoSong()); };
    addChildComponent(*rack);
    addChildComponent(*settingsView);
    showPage(rack.get());
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
    if (page != rack.get()) {
        showPage(rack.get());
        return true;
    }
    return false;
}

void MainComponent::showPage(juce::Component* newPage) {
    page = newPage;
    for (auto* candidate : { static_cast<juce::Component*>(rack.get()), static_cast<juce::Component*>(settingsView.get()) }) {
        candidate->setVisible(candidate == page);
    }
    resized();
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
    songFile = juce::File();
    songName = "Untitled";
    const auto result = session.loadSong(song);
    if (result.failed()) {
        showError(result.getErrorMessage());
    }
    refreshFromSong();
}

void MainComponent::showMenu() {
    juce::PopupMenu menu;
    menu.addItem("Undo", session.canUndo(), false, [this] { undo(); });
    menu.addItem("Redo", session.canRedo(), false, [this] { redo(); });
    menu.addSeparator();
    menu.addItem("Tempo and tap tempo...", [this] { showTempo(); });
    menu.addSeparator();
    menu.addItem("New song", [this] { loadNewSong(Song {}); });
    menu.addItem("Demo song", [this] { loadNewSong(makeDemoSong()); });
    menu.addItem("Load...", [this] { loadSong(); });
    menu.addItem("Save as...", [this] {
        songFile = juce::File();
        saveSong();
    });
    menu.addItem("Export...", [this] { exportAudio(menuButton); });
    menu.addSeparator();
    menu.addItem("Settings", true, page == settingsView.get(), [this] {
        showPage(page == settingsView.get() ? static_cast<juce::Component*>(rack.get()) : settingsView.get());
    });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&menuButton));
}

void MainComponent::showTempo() {
    juce::CallOutBox::launchAsynchronously(std::make_unique<TempoPanel>(*this), display->getScreenBounds(), nullptr);
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
    // The page above, the transport bar along the bottom.
    auto area = getLocalBounds();
    auto bar = area.removeFromBottom(Theme::isTouchDevice() ? 52 : 48).reduced(4, 4);
    const int unit = juce::jlimit(48, 96, bar.getWidth() / 11);
    songButton.setBounds(bar.removeFromLeft(unit).reduced(2, 0));
    menuButton.setBounds(bar.removeFromLeft(unit).reduced(2, 0));
    recordButton.setBounds(bar.removeFromRight(unit).reduced(2, 0));
    stopButton.setBounds(bar.removeFromRight(unit).reduced(2, 0));
    playButton.setBounds(bar.removeFromRight(unit).reduced(2, 0));
    clickButton.setBounds(bar.removeFromRight(unit).reduced(2, 0));
    modeButton.setBounds(bar.removeFromRight(unit * 3 / 2).reduced(2, 0));
    saveButton.setBounds(bar.removeFromRight(unit).reduced(2, 0));
    display->setBounds(bar.reduced(4, 0));
    if (page != nullptr) {
        page->setBounds(area);
    }
}

void MainComponent::selectMachine(int machine) {
    selectedMachine = machine;
    engine.setLiveTarget(machine); // the keyboard and MIDI devices play the machine in view
}

void MainComponent::refreshFromSong() {
    playButton.setToggleState(session.isPlaying(), juce::dontSendNotification);
    modeButton.setButtonText(session.isSongMode() ? "Song" : "Pattern");
    recordButton.setToggleState(session.isRecording(), juce::dontSendNotification);
    selectMachine(juce::jlimit(0, juce::jmax(0, session.getNumMachines() - 1), selectedMachine));
    rack->rebuild();
    resized();
}

void MainComponent::timerCallback() {
    session.collectGarbage();
    session.pollRecording();
    if (session.isInGesture() && !session.isRecording() && juce::Desktop::getInstance().getNumDraggingMouseSources() == 0) {
        session.cancelGestures(); // a press whose release went to a component that was deleted
    }
    if (--midiScanCountdown <= 0) {
        scanMidiInputs();
        midiScanCountdown = 60; // every two seconds
    }

    // Song mode stops by itself at the end of the song.
    if (playButton.getToggleState() != session.isPlaying()) {
        playButton.setToggleState(session.isPlaying(), juce::dontSendNotification);
    }
    display->repaint();
    rack->repaintPlaying();
    // The Song key is lit while the timeline is the screen in view.
    const bool atTimeline = page == rack.get() && rack->isTimelineInView();
    if (songButton.getToggleState() != atTimeline) {
        songButton.setToggleState(atTimeline, juce::dontSendNotification);
    }
}

void MainComponent::saveSong() {
    // Once the song has a file, Save writes straight to it; otherwise ask where.
    if (songFile.existsAsFile()) {
        juce::FileOutputStream stream(songFile);
        const auto result = stream.openedOk() && stream.setPosition(0) && stream.truncate().wasOk()
                                ? session.saveToStream(stream)
                                : juce::Result::fail("Could not write " + songFile.getFullPathName());
        if (result.failed()) {
            showError(result.getErrorMessage());
        }
        return;
    }
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
            return;
        }
        songFile = url.isLocalFile() ? url.getLocalFile() : juce::File();
        songName = juce::URL::removeEscapeChars(url.getFileName()).upToLastOccurrenceOf(".", false, false);
        display->repaint();
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
        } else {
            songFile = url.isLocalFile() ? url.getLocalFile() : juce::File();
            songName = juce::URL::removeEscapeChars(url.getFileName()).upToLastOccurrenceOf(".", false, false);
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
