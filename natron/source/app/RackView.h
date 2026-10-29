#pragma once

#include "core/Engine.h"
#include "core/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

// The rack: one row per machine with its name, a bank selector (A-D), and 16 pattern pads.
// Tapping a pad selects that pattern; while playing, the switch waits for the current pattern's
// end and the pad shows as queued until then.
class RackView : public juce::Component {
public:
    RackView(Session& sessionToUse, const Engine& engineToShow);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;

    int getIdealHeight() const;

private:
    Session& session;
    const Engine& engine;
    std::array<int, Engine::maxMachines> visibleBank {};

    static constexpr int rowHeight = 56;
    static constexpr int nameWidth = 110;
    static constexpr int bankWidth = 36;
    static constexpr int gap = 4;

    juce::Rectangle<int> rowBounds(int machine) const;
    juce::Rectangle<int> bankBounds(int machine, int bank) const;
    juce::Rectangle<int> padBounds(int machine, int slot) const;
};
