#pragma once

#include "ChannelStripView.h"
#include "ParameterPanel.h"
#include "core/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

// One mixer screen: the channel strips of machines first .. first + count - 1 (EQ, sends, pan,
// width, volume, mute and solo, the machine's name plate at the bottom), side by side across the
// width; or, with `master`, the master section. Strips lay their knobs out to fit the height.
class MixerView : public juce::Component {
public:
    static constexpr int stripsPerScreen = 7; // two screens hold the whole 14-machine rack

    MixerView(Session& sessionToUse, int firstMachine, bool masterScreen);
    ~MixerView() override;

    // Rebuilds the strips for the current rack.
    void rebuild();

    void resized() override;
    void paint(juce::Graphics& g) override;

private:

    Session& session;
    const int first;
    const bool showsMaster;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<ChannelStripView>> channelStrips;
    std::unique_ptr<ParameterPanel> master;
};
