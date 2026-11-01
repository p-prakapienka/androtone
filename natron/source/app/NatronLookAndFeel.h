#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Natron's look, drawn in code (no bitmaps): matte dark surfaces, dark knobs with a thin value ring in
// the machine's colour, dark keys that light up in an accent when on, an amber display. Panels and
// sections are painted by ParameterPanel, MixerView and friends with the helpers below.
class NatronLookAndFeel : public juce::LookAndFeel_V4 {
public:
    NatronLookAndFeel();

    // A slider's value-ring colour (default: the accent). Set as the slider's "capColour" property.
    static constexpr const char* capColourProperty = "capColour";

    // The palette. Inline, so other files' constants can copy them during static initialisation.
    static inline const juce::Colour background { 0xff111214 }; // behind everything
    static inline const juce::Colour surface { 0xff1b1d21 };    // panels
    static inline const juce::Colour raised { 0xff272a30 };     // keys and knobs
    static inline const juce::Colour line { 0xff33363d };       // borders, tracks
    static inline const juce::Colour text { 0xffe6e8eb };
    static inline const juce::Colour textDim { 0xff8e949c };
    static inline const juce::Colour accent { 0xffffb547 };     // lit keys, LEDs, the display
    static inline const juce::Colour lcd { 0xff0a0b0d };        // display face
    static inline const juce::Colour lcdText = accent;
    static inline const juce::Colour ink { 0xff15171a };        // text on light or lit things
    static inline const juce::Colour tabFill = line;

    // A panel: a flat surface with a hairline edge.
    static void drawPanel(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, float corner = 8.0f);
    // A section inside a panel: a slightly raised area, its title in small capitals beside a short bar
    // of `tabColour` (the machine's colour).
    static void drawSection(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour panelColour, const juce::String& title,
                            juce::Colour tabColour = accent);
    static constexpr int sectionTabHeight = 18;
    // A key: raised and dark (or `fill`), pressed in when `pressed`; and a round LED.
    static void drawKey(juce::Graphics& g, juce::Rectangle<float> key, juce::Colour fill, bool pressed);
    static void drawLed(juce::Graphics& g, juce::Rectangle<float> led, bool on);
    static juce::Font labelFont(float height);
    static juce::Font displayFont(float height);

    // Icons for the transport and toolbars, drawn into `area`.
    enum class Icon { play, stop, record, menu, timeline, save, metronome, plus, cross, octaveUp, octaveDown };
    static void drawIcon(juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour);

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPosProportional,
                          float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider) override;
    juce::Label* createSliderTextBox(juce::Slider& slider) override;
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                          float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider) override;
    void drawBubble(juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>& tip, const juce::Rectangle<float>& body) override;
    juce::Font getSliderPopupFont(juce::Slider&) override { return displayFont(15.0f); }

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW,
                      int buttonH, juce::ComboBox& box) override;
    juce::Font getComboBoxFont(juce::ComboBox& box) override;
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                              bool isMouseOverButton, bool isButtonDown) override;
    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool isMouseOverButton, bool isButtonDown) override;
    juce::Font getTextButtonFont(juce::TextButton& button, int buttonHeight) override;
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool isMouseOverButton, bool isButtonDown) override;

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;
    void drawCallOutBoxBackground(juce::CallOutBox& box, juce::Graphics& g, const juce::Path& path, juce::Image&) override;

    void drawTabButton(juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool isMouseDown) override;
    int getTabButtonBestWidth(juce::TabBarButton& button, int tabDepth) override;
    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int, int) override {}
    void drawTabbedButtonBarBackground(juce::TabbedButtonBar&, juce::Graphics&) override {}
};

// A key showing an icon instead of text (the transport, octave keys).
class IconButton : public juce::Button {
public:
    IconButton(const juce::String& name, NatronLookAndFeel::Icon iconToShow) : juce::Button(name), icon(iconToShow) {}

    void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override {
        getLookAndFeel().drawButtonBackground(g, *this, findColour(juce::TextButton::buttonColourId), isMouseOver, isButtonDown);
        const bool lit = getToggleState();
        const auto colour = lit ? (findColour(juce::TextButton::buttonOnColourId).getPerceivedBrightness() > 0.55f
                                       ? NatronLookAndFeel::ink : NatronLookAndFeel::text)
                                : (iconColour.isTransparent() ? NatronLookAndFeel::text : iconColour);
        const auto area = getLocalBounds().toFloat();
        const float size = juce::jmin(area.getWidth(), area.getHeight()) * 0.46f;
        NatronLookAndFeel::drawIcon(g, icon, area.withSizeKeepingCentre(size, size), colour.withAlpha(isEnabled() ? 1.0f : 0.35f));
    }

    juce::Colour iconColour = juce::Colours::transparentBlack; // e.g. red for record

private:
    NatronLookAndFeel::Icon icon;
};
