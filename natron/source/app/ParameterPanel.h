#pragma once

#include "PanelLayouts.h"
#include "Theme.h"
#include "TouchKnob.h"
#include "core/Parameter.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// A hardware-style panel with a control for every parameter in a ParameterSet: a knob (long-press
// for fine adjustment), an LCD menu for choices, or an LED switch for 0/1 parameters. Controls sit
// in titled sections (PanelLayouts) that flow across the width. Edits go through `onChange` (a
// Session setter), never straight to the set, so the song document stays in sync.
class ParameterPanel : public juce::Component {
public:
    using Setter = std::function<void(const juce::String& id, float value)>;

    ParameterPanel(const ParameterSet& parameters, Setter onChange, juce::String title = {});

    // Splits the controls into `type`'s sections (see PanelLayouts) on a panel of `colour`.
    void setLayout(const juce::String& type, juce::Colour colour);
    // A colour bar beside the title (the machine's colour).
    void setAccentColour(juce::Colour colour) { accentColour = colour; repaint(); }

    void resized() override;
    void paint(juce::Graphics& g) override;

    // Height needed to show every control at `width`.
    int getHeightForWidth(int width) const;

    // Re-reads every value (after a load, or a change made elsewhere).
    void refresh();

private:
    // Bigger cells on phones: knobs are turned with a finger.
    static int cellWidth() { return Theme::isTouchDevice() ? 92 : 78; }
    static int cellHeight() { return Theme::isTouchDevice() ? 108 : 94; }
    static constexpr int titleHeight = 30;
    static constexpr int padding = 6;

    struct Control {
        juce::String id;
        std::unique_ptr<TouchKnob> slider;
        std::unique_ptr<juce::ToggleButton> toggle;
        std::unique_ptr<juce::ComboBox> menu;
        std::unique_ptr<juce::Label> label;
    };

    struct Placed {
        juce::Rectangle<int> area;
        int columns = 1;
    };

    const ParameterSet& parameters;
    Setter setter;
    juce::String title;
    juce::Colour accentColour = juce::Colours::transparentBlack;
    juce::Colour panelColour { 0xffc4c8ce };
    std::vector<PanelLayouts::Section> sections;
    std::vector<Control> controls;

    // Where each section goes at `width` (same order as `sections`), and the total height.
    std::vector<Placed> place(int width, int& height) const;
};
