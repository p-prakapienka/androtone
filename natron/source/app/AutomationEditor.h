#pragma once

#include "core/Automation.h"
#include "core/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Draws and edits one automation lane:
//  - pattern mode: a lane of the given machine's selected pattern (machine, mixer, effect params)
//  - song mode: a song-automation lane (any machine's params, or the master)
// Pick the parameter from the menu; tap to add a point, drag to move it, long-press (or
// right-click) to delete it. "Clear" removes the lane.
class AutomationEditor : public juce::Component {
public:
    enum class Mode { pattern, song };

    AutomationEditor(Session& sessionToUse, Mode modeToUse);
    ~AutomationEditor() override;

    void setMachine(int machine); // pattern mode
    void refresh();               // rebuild the parameter menu and redraw

    void resized() override;

private:
    class LaneArea;

    struct Choice {
        juce::String label;
        AutomationTarget target;
        ParameterSpec spec;
    };

    Session& session;
    Mode mode;
    int machine = 0;
    std::vector<Choice> choices;

    juce::Label title;
    juce::ComboBox targetBox;
    juce::TextButton clearButton { "Clear" };
    std::unique_ptr<LaneArea> lane;

    void rebuildChoices();
    const Choice* currentChoice() const;
    double lengthInBeats() const;
    double gridInBeats() const { return mode == Mode::pattern ? Pattern::stepLength : 1.0; }

    AutomationLane currentLane() const;
    void commit(const AutomationLane& edited);
};
