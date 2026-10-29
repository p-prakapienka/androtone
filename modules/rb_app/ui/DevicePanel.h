#pragma once

namespace rb
{

// The track preset section of the instrument view (INS-2, DEV-10..DEV-22): device tabs
// (instrument, sample, two effects) and pages of parameter knobs.
class DevicePanel : public juce::Component,
                    private Studio::Listener
{
public:
    DevicePanel (Studio&, Navigator&, int trackId);
    ~DevicePanel() override;

    enum class Tab { instrument, sample, effect1, effect2 };

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    // Opens the browser for the device of a tab.
    void browse (Tab);

    std::function<void()> onTabChanged;
    bool isCollapsed() const { return collapsed; }
    void setCollapsed (bool c) { collapsed = c; resized(); repaint(); }

private:
    struct Page
    {
        juce::String name;
        std::vector<ParamTarget> params;
        bool macros = false;
    };

    void studioChanged() override;
    void rebuild();
    std::vector<Tab> availableTabs() const;
    juce::String tabLabel (Tab) const;
    std::vector<Page> pagesFor (Tab) const;

    Studio& studio;
    Navigator& nav;
    int trackId;

    Tab tab = Tab::instrument;
    int pageIndex = 0;
    std::vector<Page> pages;
    std::vector<juce::Rectangle<int>> tabAreas, pageAreas;
    std::vector<std::unique_ptr<ParamKnob>> knobs;
    std::unique_ptr<SampleView> sampleView;
    bool collapsed = false;

    // Rebuild only when something structural changes.
    juce::String structureKey;
};

} // namespace rb
