#include "MainComponent.h"

#include "core/DemoSong.h"

MainComponent::MainComponent() {
    const auto loaded = session.loadSong(makeDemoSong());
    jassert(loaded.wasOk());
    juce::ignoreUnused(loaded);

    title.setText("Natron", juce::dontSendNotification);
    title.setFont(juce::FontOptions(26.0f, juce::Font::bold));
    addAndMakeVisible(title);

    playButton.setClickingTogglesState(true);
    playButton.onClick = [this] { session.setPlaying(playButton.getToggleState()); };
    addAndMakeVisible(playButton);

    saveButton.onClick = [this] { saveSong(); };
    addAndMakeVisible(saveButton);
    loadButton.onClick = [this] { loadSong(); };
    addAndMakeVisible(loadButton);

    tempoSlider.setRange(40.0, 240.0, 1.0);
    tempoSlider.setTextValueSuffix(" BPM");
    tempoSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 90, 32);
    tempoSlider.onValueChange = [this] { session.setTempo(tempoSlider.getValue()); };
    addAndMakeVisible(tempoSlider);

    positionLabel.setJustificationType(juce::Justification::centred);
    positionLabel.setFont(juce::FontOptions(20.0f));
    addAndMakeVisible(positionLabel);

    rackPage = new RackPage(*this);
    mixerView = new MixerView(session);
    const juce::Colour tabColour(0xff282a2e);
    tabs.addTab("Rack", tabColour, rackPage, true);
    tabs.addTab("Mixer", tabColour, mixerView, true);
    tabs.setTabBarDepth(40);
    addAndMakeVisible(tabs);
    refreshFromSong();

    setSize(1000, 600);
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
    g.fillAll(juce::Colour(0xff1d1f21));
}

void MainComponent::resized() {
    auto area = getLocalBounds().reduced(16);
    auto bar = area.removeFromTop(48);
    title.setBounds(bar.removeFromLeft(130));
    playButton.setBounds(bar.removeFromLeft(100).reduced(4, 0));
    positionLabel.setBounds(bar.removeFromLeft(80));
    loadButton.setBounds(bar.removeFromRight(90).reduced(4, 0));
    saveButton.setBounds(bar.removeFromRight(90).reduced(4, 0));
    tempoSlider.setBounds(bar.reduced(8, 0));
    area.removeFromTop(12);
    tabs.setBounds(area);
}

void MainComponent::refreshFromSong() {
    tempoSlider.setValue(session.getSong().tempo, juce::dontSendNotification);
    playButton.setToggleState(session.isPlaying(), juce::dontSendNotification);
    playButton.setButtonText(session.isPlaying() ? "Stop" : "Play");
    rackPage->rack.setSelectedMachine(juce::jlimit(0, juce::jmax(0, session.getNumMachines() - 1),
                                                   rackPage->rack.getSelectedMachine()));
    rackPage->showMachine(rackPage->rack.getSelectedMachine());
    mixerView->rebuild();
    resized();
}

void MainComponent::timerCallback() {
    session.collectGarbage();

    playButton.setButtonText(session.isPlaying() ? "Stop" : "Play");
    const double beat = session.isPlaying() ? engine.getPlayheadBeat() : 0.0;
    const int bar = static_cast<int>(beat / Transport::beatsPerBar) + 1;
    const int beatInBar = static_cast<int>(std::fmod(beat, Transport::beatsPerBar)) + 1;
    positionLabel.setText(juce::String(bar) + "." + juce::String(beatInBar), juce::dontSendNotification);
    rackPage->rack.repaint();
}

MainComponent::RackPage::RackPage(MainComponent& ownerToUse) :
    rack(ownerToUse.session, ownerToUse.engine), owner(ownerToUse) {
    rack.onSelect = [this](int machine) { showMachine(machine); };
    rack.onRackChanged = [this] { owner.refreshFromSong(); };
    content.addAndMakeVisible(rack);
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);
}

void MainComponent::RackPage::showMachine(int machine) {
    machinePanel.reset();
    if (const auto* parameters = owner.session.getParameters(machine)) {
        const auto& state = owner.session.getSong().machines[static_cast<size_t>(machine)];
        machinePanel = std::make_unique<ParameterPanel>(
            *parameters,
            [this, machine](const juce::String& id, float value) { owner.session.setParameter(machine, id, value); },
            state.name + " (" + state.type + ")");
        content.addAndMakeVisible(*machinePanel);
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

        std::unique_ptr<juce::OutputStream> stream;
        if (url.isLocalFile()) {
            auto file = std::make_unique<juce::FileOutputStream>(url.getLocalFile());
            if (file->openedOk()) {
                file->setPosition(0); // FileOutputStream appends by default
                file->truncate();
                stream = std::move(file);
            }
        } else {
            stream = url.createOutputStream(); // Android content:// URIs
        }

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

void MainComponent::showError(const juce::String& message) {
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Natron", message);
}
