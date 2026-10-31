#pragma once

#include "core/PatternEditing.h"
#include "core/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <optional>

// The app's pattern clipboard (one copied pattern), and the Copy / Paste / Clear menu items the
// rack pads and the pattern editor share. Every change goes through Session, so each is one undo step.
namespace PatternClipboard {
    struct Copied {
        juce::String machineType;
        Pattern pattern;
    };

    inline std::optional<Copied>& contents() {
        static std::optional<Copied> copied;
        return copied;
    }

    inline void addItems(juce::PopupMenu& menu, Session& session, int machine, int index, std::function<void()> onChanged) {
        if (machine < 0 || machine >= session.getNumMachines()) {
            return;
        }
        const auto type = session.getSong().machines[static_cast<size_t>(machine)].type;
        const auto name = PatternBank::nameOf(index);
        menu.addItem("Copy " + name, [&session, machine, index, type] {
            contents() = Copied { type, session.getPattern(machine, index) };
        });
        menu.addItem("Paste into " + name, contents().has_value(), false, [&session, machine, index, type, onChanged] {
            const auto& copied = *contents();
            session.setPattern(machine, index, PatternEditing::forPaste(copied.pattern, copied.machineType == type));
            onChanged();
        });
        menu.addItem("Clear " + name, [&session, machine, index, onChanged] {
            auto pattern = session.getPattern(machine, index);
            PatternEditing::clear(pattern);
            session.setPattern(machine, index, pattern);
            onChanged();
        });
    }
}
