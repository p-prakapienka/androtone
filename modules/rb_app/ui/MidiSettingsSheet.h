#pragma once

namespace rb
{

// MIDI devices and mappings (MIDI-1, MIDI-3).
class MidiSettingsSheet : public Sheet,
                          private juce::Timer
{
public:
    explicit MidiSettingsSheet (Studio&);
    ~MidiSettingsSheet() override;

    void resized() override;

private:
    void rebuild();
    void timerCallback() override;

    Studio& studio;
    juce::Viewport viewport;
    juce::Component content;
    PillButton bluetoothButton { "Connect Bluetooth MIDI...", PillButton::Style::outline };
    std::vector<std::unique_ptr<juce::Component>> rows;
    juce::String lastLearning;
};

} // namespace rb
