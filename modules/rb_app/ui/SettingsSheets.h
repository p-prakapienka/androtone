#pragma once

namespace rb
{

// Set settings (SET-1..SET-4): tempo, metronome, key & scale with preview pads, export.
class SetSettingsSheet : public Sheet,
                         private Studio::Listener
{
public:
    explicit SetSettingsSheet (Studio&);
    ~SetSettingsSheet() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class PreviewPads;
    void studioChanged() override;
    void update();
    int previewTrackId() const;

    Studio& studio;

    juce::Slider tempoSlider;
    IconButton tempoDown { Icon::minus }, tempoUp { Icon::plus };
    PillButton tapButton { "Tap" };
    juce::ToggleButton metronomeToggle { "Metronome" };
    juce::ToggleButton inKeyToggle { "In Key" };
    std::vector<std::unique_ptr<PillButton>> rootButtons;
    juce::ComboBox scaleBox;
    std::unique_ptr<PreviewPads> previewPads;
    PillButton exportButton { "Export Audio...", PillButton::Style::outline };
    juce::Rectangle<int> tempoLabelArea, keyLabelArea;
};

// App settings (APP-1..APP-8).
class AppSettingsSheet : public Sheet
{
public:
    explicit AppSettingsSheet (Studio&);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    Studio& studio;
    juce::Viewport viewport;
    juce::Component content;
    juce::ComboBox themeBox, layoutBox;
    juce::Label themeLabel { {}, "Theme" }, layoutLabel { {}, "Melodic pad layout" };
    juce::ToggleButton captureTimeout { "Capture Timeout" }, noteNames { "Show Note Names" },
                       sampleNames { "Show Sample Names" }, showTouches { "Show Touches" },
                       monitorInput { "Monitor Audio Input" };
    PillButton midiButton { "MIDI Input & Mapping...", PillButton::Style::outline };
    juce::Label versionLabel;
};

// Audio export (EXP-1..EXP-3).
class ExportSheet : public Sheet,
                    private juce::Thread,
                    private juce::AsyncUpdater
{
public:
    explicit ExportSheet (Studio&);
    ~ExportSheet() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void run() override;
    void handleAsyncUpdate() override;
    void startExport();
    void togglePreview();

    Studio& studio;
    juce::ComboBox formatBox;
    PillButton exportButton { "Export", PillButton::Style::accent };
    PillButton previewButton { "Play Preview" };
    PillButton shareButton { "Share...", PillButton::Style::accent };

    Set setCopy;
    juce::File setFolder;
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> finished { false }, failed { false };
    juce::File result;
    OfflineRenderer::Format format = OfflineRenderer::Format::wav16;

    juce::AudioFormatManager formats;
    juce::AudioDeviceManager* deviceManager = nullptr;
    std::unique_ptr<juce::AudioTransportSource> transport;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioSourcePlayer player;
    juce::ScopedMessageBox shareBox;
};

} // namespace rb
