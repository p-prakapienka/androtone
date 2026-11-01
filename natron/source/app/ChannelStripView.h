#pragma once

#include "TouchKnob.h"
#include "core/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

// One machine's mixer channel: EQ, sends, pan, width and volume knobs (rings coloured by job), mute
// and solo, its rack number and a name key. Used on the mixer screens and beside each sound editor.
class ChannelStripView : public juce::Component {
public:
    static constexpr int minimumWidth = 2 * 46 + 16; // two knob columns

    ChannelStripView(Session& sessionToUse, int machineIndex);

    // Re-reads the channel's values (after an edit made on another screen).
    void refresh();

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct Knob {
        juce::String id;
        std::unique_ptr<TouchKnob> knob;
        std::unique_ptr<juce::Label> label;
    };

    static constexpr int labelHeight = 14;
    static constexpr int footerHeight = 30 + 40 + 16; // name key, M / S, margins

    Session& session;
    int machine;
    juce::String name;
    juce::Colour colour;
    std::vector<Knob> knobs;
    juce::TextButton mute { "M" };
    juce::TextButton solo { "S" };
};
