#include "MainComponent.h"

#include "core/DemoSong.h"
#include "core/SongExport.h"
#include "core/effects/Effect.h"

namespace {
    const juce::Colour background { 0xff1d1f21 };

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
    const auto loaded = session.loadSong(makeDemoSong());
    jassert(loaded.wasOk());
    juce::ignoreUnused(loaded);

    title.setText("Natron", juce::dontSendNotification);
    title.setFont(juce::FontOptions(24.0f, juce::Font::bold));
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
    exportButton.onClick = [this] { exportAudio(); };
    addAndMakeVisible(exportButton);

    tempoSlider.setRange(40.0, 240.0, 1.0);
    tempoSlider.setTextValueSuffix(" BPM");
    tempoSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 90, 32);
    tempoSlider.onValueChange = [this] { session.setTempo(tempoSlider.getValue()); };
    addAndMakeVisible(tempoSlider);

    positionLabel.setJustificationType(juce::Justification::centred);
    positionLabel.setFont(juce::FontOptions(20.0f));
    addAndMakeVisible(positionLabel);

    rackPage = new RackPage(*this);
    patternEditor = new PatternEditorView(session, engine);
    songView = new SongView(session, engine);
    mixerView = new MixerView(session);
    const juce::Colour tabColour(0xff282a2e);
    tabs.addTab("Rack", tabColour, rackPage, true);
    tabs.addTab("Pattern", tabColour, patternEditor, true);
    tabs.addTab("Song", tabColour, songView, true);
    tabs.addTab("Mixer", tabColour, mixerView, true);
    tabs.setTabBarDepth(40);
    addAndMakeVisible(tabs);
    refreshFromSong();

    setSize(1100, 640);
    setAudioChannels(0, 2);
    startTimerHz(30);
}

MainComponent::~MainComponent() {
    shutdownAudio();
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
    auto area = getLocalBounds().reduced(12);
    auto bar = area.removeFromTop(44);
    title.setBounds(bar.removeFromLeft(110));
    playButton.setBounds(bar.removeFromLeft(90).reduced(3, 0));
    modeButton.setBounds(bar.removeFromLeft(90).reduced(3, 0));
    positionLabel.setBounds(bar.removeFromLeft(70));
    exportButton.setBounds(bar.removeFromRight(84).reduced(3, 0));
    loadButton.setBounds(bar.removeFromRight(76).reduced(3, 0));
    saveButton.setBounds(bar.removeFromRight(76).reduced(3, 0));
    tempoSlider.setBounds(bar.reduced(6, 0));
    area.removeFromTop(10);
    tabs.setBounds(area);
}

void MainComponent::selectMachine(int machine) {
    rackPage->rack.setSelectedMachine(machine);
    rackPage->showMachine(machine);
    patternEditor->setMachine(machine);
}

void MainComponent::refreshFromSong() {
    tempoSlider.setValue(session.getSong().tempo, juce::dontSendNotification);
    playButton.setToggleState(session.isPlaying(), juce::dontSendNotification);
    playButton.setButtonText(session.isPlaying() ? "Stop" : "Play");
    modeButton.setButtonText(session.isSongMode() ? "Song" : "Pattern");
    selectMachine(juce::jlimit(0, juce::jmax(0, session.getNumMachines() - 1), rackPage->rack.getSelectedMachine()));
    songView->refresh();
    mixerView->rebuild();
    resized();
}

void MainComponent::timerCallback() {
    session.collectGarbage();

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

MainComponent::RackPage::RackPage(MainComponent& ownerToUse) :
    rack(ownerToUse.session, ownerToUse.engine), owner(ownerToUse) {
    rack.onSelect = [this](int machine) { owner.selectMachine(machine); };
    rack.onRackChanged = [this] { owner.refreshFromSong(); };
    content.addAndMakeVisible(rack);

    for (int slot = 0; slot < Engine::effectsPerMachine; slot++) {
        auto& controls = effectSlots[static_cast<size_t>(slot)];
        controls.label.setText("Effect " + juce::String(slot + 1), juce::dontSendNotification);
        controls.label.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        controls.type.addItem("None", 1);
        controls.type.addItemList(EffectRegistry::getTypeNames(), 2);
        controls.type.onChange = [this, slot] {
            const int machine = rack.getSelectedMachine();
            const auto& box = effectSlots[static_cast<size_t>(slot)].type;
            owner.session.setEffect(machine, slot, box.getSelectedId() == 1 ? juce::String() : box.getText());
            showMachine(machine);
        };
        content.addAndMakeVisible(controls.label);
        content.addAndMakeVisible(controls.type);
    }

    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);
}

void MainComponent::RackPage::showMachine(int machine) {
    machinePanel.reset();
    for (auto& controls : effectSlots) {
        controls.panel.reset();
    }

    const bool valid = machine >= 0 && machine < owner.session.getNumMachines();
    if (valid) {
        const auto& state = owner.session.getSong().machines[static_cast<size_t>(machine)];
        machinePanel = std::make_unique<ParameterPanel>(
            *owner.session.getParameters(machine),
            [this, machine](const juce::String& id, float value) { owner.session.setParameter(machine, id, value); },
            state.name + " (" + state.type + ")");
        content.addAndMakeVisible(*machinePanel);

        for (int slot = 0; slot < Engine::effectsPerMachine; slot++) {
            auto& controls = effectSlots[static_cast<size_t>(slot)];
            const auto& type = state.effects[static_cast<size_t>(slot)].type;
            controls.type.setText(type.isEmpty() ? "None" : type, juce::dontSendNotification);
            if (const auto* parameters = owner.session.getEffectParameters(machine, slot)) {
                controls.panel = std::make_unique<ParameterPanel>(
                    *parameters, [this, machine, slot](const juce::String& id, float value) {
                        owner.session.setEffectParameter(machine, slot, id, value);
                    });
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

void MainComponent::RackPage::resized() {
    viewport.setBounds(getLocalBounds());
    const int width = juce::jmax(1, viewport.getMaximumVisibleWidth());
    rack.setBounds(0, 8, width, rack.getIdealHeight());
    int bottom = rack.getBottom();
    if (machinePanel != nullptr) {
        machinePanel->setBounds(0, bottom + 12, width, machinePanel->getHeightForWidth(width));
        bottom = machinePanel->getBottom();
    }
    for (auto& controls : effectSlots) {
        if (!controls.type.isVisible()) {
            continue;
        }
        controls.label.setBounds(4, bottom + 12, 90, 32);
        controls.type.setBounds(100, bottom + 12, 180, 32);
        bottom += 12 + 32;
        if (controls.panel != nullptr) {
            controls.panel->setBounds(0, bottom + 4, width, controls.panel->getHeightForWidth(width));
            bottom = controls.panel->getBottom();
        }
    }
    content.setSize(width, bottom + 12);
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

void MainComponent::exportAudio() {
    juce::PopupMenu menu;
    menu.addItem(1, "Export song (timeline)", session.getSong().endBar() > 0);
    menu.addItem(2, "Export pattern loop (4 bars)");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&exportButton), [this](int result) {
        if (result > 0) {
            startExport(result == 1);
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
