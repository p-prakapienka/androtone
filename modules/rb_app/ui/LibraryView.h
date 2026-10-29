#pragma once

namespace rb
{

// The set library (LIB-1..LIB-6): user sets, demo sets, new set, import, app settings.
class LibraryView : public juce::Component,
                    private Studio::Listener,
                    private juce::Timer
{
public:
    LibraryView (Studio&, Navigator&);
    ~LibraryView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Row;
    void refresh();
    void studioChanged() override;
    void timerCallback() override;
    void openSet (const SetInfo&);
    void showOptions (const SetInfo&, juce::Component& anchor);
    void importFile();

    Studio& studio;
    Navigator& nav;

    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<Row>> rows;
    std::vector<std::unique_ptr<juce::Label>> sectionLabels;

    IconButton settingsButton { Icon::dots };
    IconButton importButton { Icon::folder };
    PillButton newButton { "+ New", PillButton::Style::accent };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ScopedMessageBox shareBox;
};

} // namespace rb
