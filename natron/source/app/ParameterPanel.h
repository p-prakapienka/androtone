#pragma once

#include "NatronLookAndFeel.h"
#include "PanelLayouts.h"
#include "Theme.h"
#include "TouchKnob.h"
#include "core/Parameter.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// A panel with a control for every parameter in a ParameterSet: a knob (long-press for fine
// adjustment; its value shows while turning), a menu for choices, or an LED key for 0/1
// parameters. Controls sit in titled sections (PanelLayouts) that fill the whole panel: sections
// go into rows, each row's sections share its width, and the arrangement that gives the biggest
// knobs wins, so a machine always shows complete on one screen. Edits go through `onChange` (a
// Session setter), never straight to the set, so the song document stays in sync.
class ParameterPanel : public juce::Component {
public:
    using Setter = std::function<void(const juce::String& id, float value)>;

    ParameterPanel(const ParameterSet& parameters, Setter onChange, juce::String title = {});

    // Splits the controls into `type`'s sections (see PanelLayouts) on a panel of `colour`.
    void setLayout(const juce::String& type, juce::Colour colour);
    // The machine's (or effect's) colour: value rings, section marks and the title mark.
    void setAccentColour(juce::Colour colour);

    void resized() override;
    void paint(juce::Graphics& g) override;

    // Re-reads every value (after a load, or a change made elsewhere).
    void refresh();

private:
    struct Control {
        juce::String id;
        std::unique_ptr<TouchKnob> slider;
        std::unique_ptr<juce::ToggleButton> toggle;
        std::unique_ptr<juce::ComboBox> menu;
        std::unique_ptr<juce::Label> label;
    };

    // Where a section goes, and the grid of its controls.
    struct Placed {
        juce::Rectangle<int> area;
        int columns = 1;
        int cellWidth = 0;
        int cellHeight = 0;
    };

    static constexpr int titleHeight = 26;
    static constexpr int gap = 6;
    static constexpr int labelHeight = 14;

    const ParameterSet& parameters;
    Setter setter;
    juce::String title;
    juce::Colour accentColour = NatronLookAndFeel::accent;
    juce::Colour panelColour = NatronLookAndFeel::surface;
    std::vector<PanelLayouts::Section> sections;
    std::vector<Control> controls;
    std::vector<Placed> placed;

    // Finds the arrangement with the biggest knobs for the current size.
    void arrange();
};
