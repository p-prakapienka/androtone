#pragma once

namespace rb
{

// Browser for track presets, effect presets and samples (DEV-3, DEV-4, DEV-23, SMP-2).
// STUB: minimal list until the full browser lands.
class BrowserSheet : public Sheet
{
public:
    enum class Mode { trackPreset, effect1, effect2, sample };

    BrowserSheet (Studio&, int trackId, Mode);
    void resized() override;

private:
    Studio& studio;
    int trackId;
    Mode mode;
    juce::ListBox list;
    struct Model;
    std::unique_ptr<Model> model;
};

} // namespace rb
