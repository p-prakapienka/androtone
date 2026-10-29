#pragma once

namespace rb
{

// Root component of the app window.
class MainComponent : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AudioEngine engine;
};

} // namespace rb
