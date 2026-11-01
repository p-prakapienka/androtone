#pragma once

#include "AutomationEditor.h"
#include "NatronLookAndFeel.h"
#include "core/Engine.h"
#include "core/Session.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Touch piano roll for one machine's pattern (the rack's selected machine, its selected pattern).
//  - tap an empty cell: add a 1/16 note; tap a note: remove it
//  - drag a note: move it; drag its right edge: resize (overlap the next note on Acid = slide)
//  - long-press a note: cycle velocity, default → accent → soft
//  - pinch (or trackpad magnify / ctrl+wheel): zoom horizontally
//  - Pattern... menu: copy / paste / clear, transpose, double the length
// Drums show one row per kit sound (a step grid); other machines show a keyboard range.
// Toolbar: the Mode key (top left: the screen shows the notes or the pattern's automation lanes),
// bank and pattern keys, Size, and Edit (copy/paste, transpose, double, quantise).
class PatternEditorView : public juce::Component, private juce::Timer {
public:
    explicit PatternEditorView(Session& sessionToUse, const Engine& engineToShow);
    ~PatternEditorView() override;

    // Edits `machine`'s selected pattern from now on.
    void setMachine(int machine);
    void refresh();

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    class Grid;
    class PatternKeys;

    Session& session;
    const Engine& engine;
    int machine = 0;

    IconButton modeButton { "Mode", NatronLookAndFeel::Icon::notes }; // top left: notes or automation
    std::unique_ptr<PatternKeys> patternKeys;     // bank A-D and pattern 1-16 keys
    juce::ComboBox lengthBox;                     // Size: 1, 2, 4 or 8 bars
    juce::TextButton toolsButton { "Edit" };      // copy/paste/clear, transpose, double, quantise
    bool showingAutomation = false;
    juce::Viewport viewport;
    std::unique_ptr<Grid> grid;
    AutomationEditor automation;

    void timerCallback() override;
    void showToolsMenu();
    void editPattern(const std::function<bool(Pattern&)>& change);
};
