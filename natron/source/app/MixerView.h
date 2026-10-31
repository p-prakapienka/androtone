#pragma once

#include "ParameterPanel.h"
#include "core/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The mixer console: one vertical channel strip per machine (EQ, sends, pan, width, volume, mute
// and solo, the machine's name plate at the bottom), then the master section, side by side in a
// full-height strip that scrolls sideways. Strips lay their knobs out to fit the height.
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
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<Strip>> channelStrips;
    std::unique_ptr<ParameterPanel> master;
};
