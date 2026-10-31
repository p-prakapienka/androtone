#pragma once

#include "ParameterPanel.h"
#include "core/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The mixer console: one vertical channel strip per machine (EQ, sends, pan, width, volume, mute
// and solo, the machine's name plate at the bottom), side by side and scrolling sideways, with the
// master section below.
class MixerView : public juce::Component {
public:
    explicit MixerView(Session& sessionToUse);

    // Rebuilds the strips for the current rack.
    void rebuild();

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    class Strip;

    Session& session;
    juce::Viewport stripViewport;
    juce::Component strips;
    std::vector<std::unique_ptr<Strip>> channelStrips;
    juce::Viewport masterViewport;
    std::unique_ptr<ParameterPanel> master;
};
