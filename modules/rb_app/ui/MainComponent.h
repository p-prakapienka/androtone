#pragma once

namespace rb
{

// What views use to move between screens and open sheets.
class Navigator
{
public:
    virtual ~Navigator() = default;
    virtual void showLibrary() = 0;
    virtual void showSession() = 0;
    virtual void showInstrument (int trackId, int slot) = 0;
    virtual void showSheet (std::unique_ptr<Sheet>) = 0;
    virtual void closeSheet() = 0;
};

// Root component: owns the Studio, the current screen and the sheet layer.
class MainComponent : public juce::Component,
                      public Navigator,
                      private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void showLibrary() override;
    void showSession() override;
    void showInstrument (int trackId, int slot) override;
    void showSheet (std::unique_ptr<Sheet>) override;
    void closeSheet() override;

    Studio& getStudio() { return *studio; }

    // Renders each main screen to PNG files (used for headless visual checks).
    void writeSnapshots (const juce::File& folder);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void setView (std::unique_ptr<juce::Component>);

    struct Dimmer : public juce::Component
    {
        std::function<void()> onTap;
        void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black.withAlpha (0.55f)); }
        void mouseUp (const juce::MouseEvent&) override { if (onTap) onTap(); }
    };

    struct TouchIndicator : public juce::Component, private juce::Timer
    {
        TouchIndicator();
        ~TouchIndicator() override;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void timerCallback() override;
        std::map<int, juce::Point<float>> touches;
        std::map<int, float> fades;
    };

    RbLookAndFeel lookAndFeel;
    std::unique_ptr<Studio> studio;
    std::unique_ptr<juce::Component> view;
    Dimmer dimmer;
    std::unique_ptr<Sheet> sheet;
    std::unique_ptr<TouchIndicator> touchIndicator;
};

} // namespace rb
