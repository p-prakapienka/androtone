#include "FxRackView.h"

#include "NatronLookAndFeel.h"
#include "Theme.h"
#include "core/effects/Effect.h"

FxRackView::FxRackView(Session& sessionToUse, int firstMachine) :
    session(sessionToUse), first(firstMachine) {
    rebuild();
}

void FxRackView::rebuild() {
    slots.clear();
    for (int m = first; m < juce::jmin(first + machinesPerScreen, session.getNumMachines()); m++) {
        const auto& state = session.getSong().machines[static_cast<size_t>(m)];
        for (int s = 0; s < Engine::effectsPerMachine; s++) {
            auto slot = std::make_unique<Slot>();
            slot->machine = m;
            slot->index = s;
            const auto& type = state.effects[static_cast<size_t>(s)].type;
            if (const auto* parameters = session.getEffectParameters(m, s)) {
                slot->pedal = std::make_unique<ParameterPanel>(
                    *parameters, [this, m, s](const juce::String& id, float value) { session.setEffectParameter(m, s, id, value); },
                    type);
                slot->pedal->setLayout({}, Theme::effectColour(type).darker(0.55f));
                slot->pedal->setAccentColour(Theme::effectColour(type));
                slot->pedal->setCompact(true);
                slot->pedal->setFitToBox(true); // every knob shows, however small the slot
                addAndMakeVisible(*slot->pedal);
                slot->removeButton.onClick = [this, m, s] {
                    session.setEffect(m, s, {});
                    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<FxRackView>(this)] {
                        if (safe != nullptr) {
                            safe->rebuild();
                        }
                    });
                };
                addAndMakeVisible(slot->removeButton);
            } else {
                slot->addButton.onClick = [this, raw = slot.get()] { showAddMenu(*raw); };
                addAndMakeVisible(slot->addButton);
            }
            slots.push_back(std::move(slot));
        }
    }
    resized();
    repaint();
}

void FxRackView::showAddMenu(Slot& slot) {
    juce::PopupMenu menu;
    const auto types = EffectRegistry::getTypeNames();
    for (int i = 0; i < types.size(); i++) {
        menu.addItem(i + 1, types[i]);
    }
    const int machine = slot.machine;
    const int index = slot.index;
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&slot.addButton),
                       [this, types, machine, index](int result) {
                           if (result > 0) {
                               session.setEffect(machine, index, types[result - 1]);
                               rebuild();
                           }
                       });
}

juce::Rectangle<int> FxRackView::slotBounds(int column, int index) const {
    const int width = getWidth() / machinesPerScreen;
    const int height = (getHeight() - nameHeight) / Engine::effectsPerMachine;
    return { column * width, nameHeight + index * height, width, height };
}

void FxRackView::paint(juce::Graphics& g) {
    g.fillAll(NatronLookAndFeel::background);
    const int width = getWidth() / machinesPerScreen;
    for (int c = 0; c < machinesPerScreen; c++) {
        // The machine's name key (blank when there's no machine here).
        auto key = juce::Rectangle<int>(c * width, 0, width, nameHeight).reduced(3).toFloat();
        const int machine = first + c;
        const bool exists = machine < session.getNumMachines();
        NatronLookAndFeel::drawKey(g, key, exists ? juce::Colour(0xffe3e5e8) : juce::Colour(0xff8d9198), false);
        if (exists) {
            const auto& state = session.getSong().machines[static_cast<size_t>(machine)];
            g.setColour(Theme::machineColour(state.type));
            g.fillRect(key.removeFromLeft(5.0f).reduced(0.0f, 4.0f));
            g.setColour(NatronLookAndFeel::ink);
            g.setFont(NatronLookAndFeel::labelFont(12.0f));
            g.drawFittedText(state.name, key.toNearestInt().reduced(4, 0), juce::Justification::centred, 1);
        }
        // Empty bays behind the slots.
        for (int s = 0; s < Engine::effectsPerMachine; s++) {
            const auto bay = slotBounds(c, s).reduced(3).toFloat();
            g.setColour(juce::Colour(0xff222326));
            g.fillRoundedRectangle(bay, 6.0f);
            g.setColour(juce::Colour(0xff3a3c40));
            g.drawRoundedRectangle(bay.reduced(0.5f), 6.0f, 1.0f);
        }
    }
}

void FxRackView::resized() {
    for (auto& slot : slots) {
        const auto bay = slotBounds(slot->machine - first, slot->index).reduced(3);
        if (slot->pedal != nullptr) {
            slot->pedal->setBounds(bay);
            slot->removeButton.setBounds(bay.getRight() - 30, bay.getBottom() - 30, 26, 26);
            slot->removeButton.toFront(false);
        } else {
            slot->addButton.setBounds(bay.withSizeKeepingCentre(juce::jmin(56, bay.getWidth() - 8), juce::jmin(56, bay.getHeight() - 8)));
        }
    }
}
