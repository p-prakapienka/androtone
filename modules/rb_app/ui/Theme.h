#pragma once

namespace rb
{

// Colours and metrics for the touch UI (APP-2: Dark / High Contrast).
struct Theme
{
    juce::Colour background, surface, surfaceRaised, outline, text, textDim, accent, playing, record, danger;

    static const Theme& get();
    static void use (AppSettings::Theme);

    static juce::Colour trackColour (int index);

    // Touch metrics in logical pixels (dp on Android).
    static constexpr float corner = 8.0f;
    static constexpr int touch = 44;    // minimum touch target
    static constexpr int barHeight = 56;
    static constexpr int gap = 6;

    static juce::Font font (float height, bool bold = false);
};

// Simple vector icons drawn into a unit box (0..1).
enum class Icon
{
    play, stop, record, back, gear, dots, plus, minus, close, undo, redo, mixer, scenes, duplicate,
    quantize, expand, collapse, heart, heartFilled, search, mic, folder, trash, pads, keys, single,
    repeat, octave, save, share, check, triplet, chevronDown, chevronUp, chevronLeft, chevronRight,
    metronome, waveform, filter, edit, crop, loop, drum, synth, sampler, wave, star
};

juce::Path makeIcon (Icon);
void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float> area, juce::Colour, float strokeWidth = 1.8f);

class RbLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RbLookAndFeel();
    void refreshColours();

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    juce::Font getPopupMenuFont() override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    juce::Font getLabelFont (juce::Label&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    int getDefaultScrollbarWidth() override { return 4; }
};

} // namespace rb
