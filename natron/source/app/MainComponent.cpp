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

    addAndMakeVisible(rack);
    refreshFromSong();

    setSize(1000, 480);
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
    area.removeFromTop(16);
    rack.setBounds(area.removeFromTop(juce::jmin(area.getHeight(), rack.getIdealHeight())));
}

void MainComponent::refreshFromSong() {
    tempoSlider.setValue(session.getSong().tempo, juce::dontSendNotification);
    playButton.setToggleState(session.isPlaying(), juce::dontSendNotification);
    playButton.setButtonText(session.isPlaying() ? "Stop" : "Play");
    resized();
    rack.repaint();
}

void MainComponent::timerCallback() {
    session.collectGarbage();

    playButton.setButtonText(session.isPlaying() ? "Stop" : "Play");
    const double beat = session.isPlaying() ? engine.getPlayheadBeat() : 0.0;
    const int bar = static_cast<int>(beat / Transport::beatsPerBar) + 1;
    const int beatInBar = static_cast<int>(std::fmod(beat, Transport::beatsPerBar)) + 1;
    positionLabel.setText(juce::String(bar) + "." + juce::String(beatInBar), juce::dontSendNotification);
    rack.repaint();
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
