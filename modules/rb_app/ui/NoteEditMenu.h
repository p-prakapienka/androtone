#pragma once

namespace rb
{

// The note-edit menu of the expanded MIDI editor (EDIT-10). Tabs:
//  - Operations: editor grid value (Off, 1/4..1/32, triplets); shorten, nudge, delete, extend.
//  - Transpose: +-1 step (scale steps when In Key, pads for drums), +-octave (melodic only).
//  - Quantization: grid value, triplets, Off (restore timing), amount. Shared with the action
//    bar's quantize button through InstrumentView::quantizeGrid/Strength/Triplets.
//  - Velocity: one slider per note; swipe to draw, two fingers for a linear ramp.
//  - Pitches (drum clips only): per-hit pitch offset sliders.
// Everything applies to the selection, or to all notes in the loop when nothing is selected.
class NoteEditMenu : public juce::Component
{
public:
    enum Tab { operationsTab = 0, transposeTab, quantizeTab, velocityTab, pitchesTab, numTabs };

    explicit NoteEditMenu (ClipEditor&);
    ~NoteEditMenu() override;

    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class ValueBars;

    int currentTab() const;
    void showTab (int);
    void applyToTargets (const std::function<ClipEdit (const Clip&, const Selection&)>&);
    void applyQuantize();
    void auditionFirstTarget();
    std::vector<juce::Component*> componentsOf (int tab);

    ClipEditor& owner;

    std::vector<std::unique_ptr<PillButton>> tabButtons;

    // Operations
    std::vector<std::unique_ptr<PillButton>> gridButtons; // Off, divisions..., T
    IconButton shortenButton { Icon::minus, "Shorten" }, nudgeLeftButton { Icon::chevronLeft, "Nudge" },
               deleteButton { Icon::trash, "Delete" }, nudgeRightButton { Icon::chevronRight, "Nudge" },
               extendButton { Icon::plus, "Extend" };

    // Transpose
    IconButton stepDownButton { Icon::chevronDown, "Step -1" }, stepUpButton { Icon::chevronUp, "Step +1" },
               octaveDownButton { Icon::chevronDown, "Oct -1" }, octaveUpButton { Icon::chevronUp, "Oct +1" };

    // Quantization
    std::vector<std::unique_ptr<PillButton>> quantizeButtons; // Off, divisions..., T
    juce::Slider amountSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    bool quantizeOff = false;
    bool amountDragging = false;

    std::unique_ptr<ValueBars> velocityBars, pitchBars;

    juce::Rectangle<int> captionArea, amountLabelArea;
};

} // namespace rb
