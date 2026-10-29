#pragma once

#include "core/Parameter.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Builds a control for every parameter in a ParameterSet: a rotary knob, or a toggle for 0/1
// parameters. Edits go through `onChange` (a Session setter), never straight to the set, so the
// song document stays in sync. Lays itself out as a wrapping grid.
class ParameterPanel : public juce::Component {
public:
    using Setter = std::function<void(const juce::String& id, float value)>;

    ParameterPanel(const ParameterSet& parameters, Setter onChange, juce::String title = {});

    void resized() override;
    void paint(juce::Graphics& g) override;

    // Height needed to show every control at `width`.
    int getHeightForWidth(int width) const;

    // Re-reads every value (after a load, or a change made elsewhere).
    void refresh();

private:
    static constexpr int cellWidth = 84;
    static constexpr int cellHeight = 96;
    static constexpr int titleHeight = 28;

    struct Control {
        juce::String id;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ToggleButton> toggle;
        std::unique_ptr<juce::Label> label;
    };

    const ParameterSet& parameters;
    Setter setter;
    juce::String title;
    std::vector<Control> controls;

    int columnsFor(int width) const { return juce::jmax(1, width / cellWidth); }
};
