#pragma once

#include "AutomationEditor.h"
#include "core/Engine.h"
#include "core/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The song timeline: one row per machine, one column per bar, pattern blocks on the rows.
//  - tap an empty bar: place the machine's selected pattern there (as long as the pattern)
//  - drag a block: move it; drag its right edge: resize it
//  - long-press or right-click a block: Duplicate / Delete menu
//  - drag across the ruler: set the loop region (and turn looping on); tap the ruler: toggle looping
//  - pinch / magnify / ctrl+wheel: zoom
// Above: the song's swing; below: the song automation lane editor.
class SongView : public juce::Component, private juce::Timer {
public:
    SongView(Session& sessionToUse, const Engine& engineToShow);

    void refresh();
    void resized() override;

private:
    class Timeline;

    Session& session;
    const Engine& engine;
    juce::Viewport viewport;
    std::unique_ptr<Timeline> timeline;
    juce::Label swingLabel;
    juce::Slider swing;
    AutomationEditor automation;
    juce::TextButton automationButton { "Automation" }; // folds the lane away (folded on phones)

    void timerCallback() override;
};
