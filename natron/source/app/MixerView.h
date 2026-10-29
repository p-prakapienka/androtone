#pragma once

#include "ParameterPanel.h"
#include "core/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

// One channel panel per machine, then the master section, in a scrolling column.
class MixerView : public juce::Component {
public:
    explicit MixerView(Session& sessionToUse);

    // Rebuilds the panels for the current rack.
    void rebuild();

    void resized() override;

private:
    Session& session;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<ParameterPanel>> panels;
};
