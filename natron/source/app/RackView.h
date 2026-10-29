#pragma once

#include "core/Engine.h"
#include "core/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>

// The rack: one row per machine with its name, a bank selector (A-D), and 16 pattern pads.
// Tapping a pad selects that pattern; while playing, the switch waits for the current pattern's
// end and the pad shows as queued until then. Tapping a name selects the machine (its panel is
// shown below the rack); long-pressing or right-clicking it offers Remove. A final row adds a
// machine.
class RackView : public juce::Component {
public:
    RackView(Session& sessionToUse, const Engine& engineToShow);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

    int getIdealHeight() const;

    int getSelectedMachine() const { return selected; }
    void setSelectedMachine(int machine) { selected = machine; repaint(); }

    std::function<void(int machine)> onSelect;
    std::function<void()> onRackChanged; // a machine was added or removed

private:
    Session& session;
    const Engine& engine;
    std::array<int, Engine::maxMachines> visibleBank {};
    int selected = 0;
    int pressedName = -1;

    static constexpr int rowHeight = 56;
    static constexpr int nameWidth = 110;
    static constexpr int bankWidth = 36;
    static constexpr int gap = 4;

    juce::Rectangle<int> rowBounds(int machine) const;
    juce::Rectangle<int> nameBounds(int machine) const;
    juce::Rectangle<int> addBounds() const;
    void showAddMenu();
    void showMachineMenu(int machine);
    juce::Rectangle<int> bankBounds(int machine, int bank) const;
    juce::Rectangle<int> padBounds(int machine, int slot) const;
};
