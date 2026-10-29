#pragma once

#include "core/Engine.h"
#include "core/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Touch piano roll for one machine's pattern (the rack's selected machine, its selected pattern).
//  - tap an empty cell: add a 1/16 note; tap a note: remove it
//  - drag a note: move it; drag its right edge: resize (overlap the next note on Acid = slide)
//  - long-press a note: cycle velocity, default → accent → soft
//  - pinch (or trackpad magnify / ctrl+wheel): zoom horizontally
// Drums show one row per kit sound (a step grid); other machines show a keyboard range.
class PatternEditorView : public juce::Component, private juce::Timer {
public:
    PatternEditorView(Session& sessionToUse, const Engine& engineToShow);

    // Edits `machine`'s selected pattern from now on.
    void setMachine(int machine);
    void refresh();

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    class Grid;

    Session& session;
    const Engine& engine;
    int machine = 0;

    juce::Label title;
    juce::ComboBox lengthBox;
    juce::Viewport viewport;
    std::unique_ptr<Grid> grid;

    void timerCallback() override;
};
