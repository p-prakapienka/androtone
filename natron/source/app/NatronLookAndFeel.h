#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Natron's hardware look, drawn in code (no bitmaps): chrome knobs with a dark rim, dark LCD
// readouts and menus, bevelled light-grey buttons, LED toggles, bold capital labels. Panels and
// sections are painted by ParameterPanel and MixerView with the helpers below.
class NatronLookAndFeel : public juce::LookAndFeel_V4 {
public:
    NatronLookAndFeel();

    // Knob cap colour for one slider (default: chrome). Set as the slider's "capColour" property.
    static constexpr const char* capColourProperty = "capColour";

    // Colours shared with the panels. Inline, so other files' constants can copy them safely
    // during static initialisation.
    static inline const juce::Colour background { 0xff2a2b2e }; // the charcoal behind everything
    static inline const juce::Colour lcd { 0xff141a22 };        // LCD face
    static inline const juce::Colour lcdText { 0xff9fe8c8 };
    static inline const juce::Colour ink { 0xff23262b };        // labels on light panels
    static inline const juce::Colour tabFill { 0xff1c1e22 };    // section header tabs

    // A hardware panel: rounded, gradient fill, darker edge.
    static void drawPanel(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, float corner = 6.0f);
    // A section inside a panel, with its title on a dark tab at the top left.
    // On a dark panel the tab takes `tabColour` (the machine's colour) and dark text.
    static void drawSection(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour panelColour, const juce::String& title,
                            juce::Colour tabColour = tabFill);
    static constexpr int sectionTabHeight = 20;
    // A bevelled key in `fill`, drawn pressed in when `pressed`; and the round LED keys carry.
    static void drawKey(juce::Graphics& g, juce::Rectangle<float> key, juce::Colour fill, bool pressed);
    static void drawLed(juce::Graphics& g, juce::Rectangle<float> led, bool on);
    static juce::Font labelFont(float height);

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPosProportional,
                          float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider) override;
    juce::Label* createSliderTextBox(juce::Slider& slider) override;
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                          float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider) override;

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW,
                      int buttonH, juce::ComboBox& box) override;
    juce::Font getComboBoxFont(juce::ComboBox& box) override;
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                              bool isMouseOverButton, bool isButtonDown) override;
    juce::Font getTextButtonFont(juce::TextButton& button, int buttonHeight) override;
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool isMouseOverButton, bool isButtonDown) override;

    void drawTabButton(juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool isMouseDown) override;
    int getTabButtonBestWidth(juce::TabBarButton& button, int tabDepth) override;
    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int, int) override {}
    void drawTabbedButtonBarBackground(juce::TabbedButtonBar&, juce::Graphics&) override {}
};
