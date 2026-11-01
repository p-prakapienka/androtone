#pragma once

#include "ParameterPanel.h"
#include "core/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

// One effects screen: a column for each of machines first .. first + 6 under its name key, each with
// two insert-effect slots. A filled slot is a pedal (the effect's colour, its knobs, an X key that
// removes it); an empty one is a bay with a "+" key that offers the effect types.
class FxRackView : public juce::Component {
public:
    static constexpr int machinesPerScreen = 7; // two screens hold the whole 14-machine rack

    FxRackView(Session& sessionToUse, int firstMachine);

    void rebuild();
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    struct Slot {
        int machine = 0;
        int index = 0;
        std::unique_ptr<ParameterPanel> pedal;
        IconButton removeButton { "Remove effect", NatronLookAndFeel::Icon::cross };
        IconButton addButton { "Add effect", NatronLookAndFeel::Icon::plus };
    };

    Session& session;
    const int first;
    std::vector<std::unique_ptr<Slot>> slots; // machine by machine, two each

    static constexpr int nameHeight = 30;
    juce::Rectangle<int> slotBounds(int column, int index) const;
    void showAddMenu(Slot& slot);
};
