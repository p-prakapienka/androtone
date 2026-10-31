#include "RackView.h"

#include "PatternClipboard.h"
#include "Theme.h"
#include "core/MachineRegistry.h"

namespace {
    const juce::Colour background { 0xff1d1f21 };
    const juce::Colour rowColour { 0xff282a2e };
    const juce::Colour emptyPad { 0xff373b41 };
    const juce::Colour accent { 0xffde935f };
    const juce::Colour text { 0xffc5c8c6 };
}

RackView::RackView(Session& sessionToUse, const Engine& engineToShow) :
    session(sessionToUse), engine(engineToShow) {}

int RackView::getIdealHeight() const {
    return (session.getNumMachines() + 1) * (rowHeight + gap);
}

juce::Rectangle<int> RackView::nameBounds(int machine) const {
    return rowBounds(machine).withWidth(nameWidth);
}

juce::Rectangle<int> RackView::addBounds() const {
    return rowBounds(session.getNumMachines()).withWidth(nameWidth + PatternBank::numBanks * (bankWidth + gap));
}

juce::Rectangle<int> RackView::rowBounds(int machine) const {
    return { 0, machine * (rowHeight + gap), getWidth(), rowHeight };
}

juce::Rectangle<int> RackView::bankBounds(int machine, int bank) const {
    return rowBounds(machine).withTrimmedLeft(nameWidth + bank * (bankWidth + gap)).withWidth(bankWidth).reduced(0, 8);
}

juce::Rectangle<int> RackView::padBounds(int machine, int slot) const {
    const auto row = rowBounds(machine);
    const int padsLeft = nameWidth + PatternBank::numBanks * (bankWidth + gap) + 2 * gap;
    const int padWidth = juce::jmax(8, (row.getWidth() - padsLeft) / PatternBank::patternsPerBank);
    return { row.getX() + padsLeft + slot * padWidth, row.getY() + 6, padWidth - gap, rowHeight - 12 };
}

void RackView::paint(juce::Graphics& g) {
    g.fillAll(background);

    if (session.getNumMachines() < Engine::maxMachines) {
        const auto add = addBounds().reduced(0, 8).toFloat();
        g.setColour(rowColour);
        g.fillRoundedRectangle(add, 6.0f);
        g.setColour(text);
        g.drawText("+ Add machine", add, juce::Justification::centred);
    }

    const bool blinkOn = (juce::Time::getMillisecondCounter() / 250) % 2 == 0;

    for (int m = 0; m < session.getNumMachines(); m++) {
        const auto& state = session.getSong().machines[static_cast<size_t>(m)];
        const auto row = rowBounds(m);
        const auto colour = Theme::machineColour(state.type);
        g.setColour(rowColour);
        g.fillRoundedRectangle(row.toFloat(), 6.0f);
        g.setColour(colour);
        g.fillRoundedRectangle(row.toFloat().withWidth(6.0f), 3.0f);
        if (m == selected) {
            g.setColour(accent);
            g.drawRoundedRectangle(row.toFloat().reduced(1.0f), 6.0f, 2.0f);
        }

        g.setColour(text);
        g.setFont(juce::FontOptions(16.0f));
        const auto label = nameBounds(m).reduced(10, 8);
        g.drawText(state.name, label.withHeight(22), juce::Justification::centredLeft);
        g.setFont(juce::FontOptions(11.0f));
        g.setColour(text.withAlpha(0.6f));
        g.drawText(state.type, label.withTrimmedTop(24), juce::Justification::centredLeft);

        const int current = m < engine.getNumMachines() ? engine.getCurrentPattern(m) : state.selectedPattern;
        const int queued = m < engine.getNumMachines() ? engine.getQueuedPattern(m) : -1;
        const int bank = visibleBank[static_cast<size_t>(m)];

        for (int b = 0; b < PatternBank::numBanks; b++) {
            const auto bounds = bankBounds(m, b).toFloat();
            g.setColour(b == bank ? text : emptyPad);
            g.fillRoundedRectangle(bounds, 4.0f);
            g.setColour(b == bank ? background : text);
            g.drawText(juce::String::charToString(static_cast<juce::juce_wchar>('A' + b)), bounds,
                       juce::Justification::centred);
        }

        for (int slot = 0; slot < PatternBank::patternsPerBank; slot++) {
            const int index = PatternBank::indexOf(bank, slot);
            const auto bounds = padBounds(m, slot).toFloat();
            const bool hasNotes = !state.patterns[static_cast<size_t>(index)].isEmpty();

            g.setColour(index == current ? accent : (hasNotes ? colour.withMultipliedBrightness(0.75f) : emptyPad));
            g.fillRoundedRectangle(bounds, 4.0f);
            if (index == queued && blinkOn) {
                g.setColour(accent);
                g.drawRoundedRectangle(bounds.reduced(1.0f), 4.0f, 2.0f);
            }
            g.setColour(index == current ? background : text);
            g.setFont(juce::FontOptions(12.0f));
            g.drawText(juce::String(slot + 1), bounds, juce::Justification::centred);
        }
    }
}

void RackView::mouseDown(const juce::MouseEvent& event) {
    const auto position = event.getPosition();
    if (session.getNumMachines() < Engine::maxMachines && addBounds().contains(position)) {
        showAddMenu();
        return;
    }
    for (int m = 0; m < session.getNumMachines(); m++) {
        if (!rowBounds(m).contains(position)) {
            continue;
        }
        if (nameBounds(m).contains(position)) {
            if (event.mods.isPopupMenu()) {
                showMachineMenu(m);
            } else {
                pressedName = m; // tap or long press: decided in mouseUp
            }
            return;
        }
        for (int b = 0; b < PatternBank::numBanks; b++) {
            if (bankBounds(m, b).contains(position)) {
                visibleBank[static_cast<size_t>(m)] = b;
                repaint();
                return;
            }
        }
        for (int slot = 0; slot < PatternBank::patternsPerBank; slot++) {
            if (padBounds(m, slot).contains(position)) {
                const int index = PatternBank::indexOf(visibleBank[static_cast<size_t>(m)], slot);
                if (event.mods.isPopupMenu()) {
                    showPatternMenu(m, index);
                    return;
                }
                session.selectPattern(m, index);
                pressedPadMachine = m;
                pressedPad = index;
                repaint();
                return;
            }
        }
    }
}

void RackView::mouseUp(const juce::MouseEvent& event) {
    if (pressedPad >= 0) {
        const int padMachine = pressedPadMachine;
        const int pad = pressedPad;
        pressedPadMachine = pressedPad = -1;
        if (event.getLengthOfMousePress() >= 500 && event.getDistanceFromDragStart() < 10) {
            showPatternMenu(padMachine, pad);
        }
        return;
    }
    const int machine = pressedName;
    pressedName = -1;
    if (machine < 0 || machine >= session.getNumMachines() || !nameBounds(machine).contains(event.getPosition())) {
        return;
    }
    if (event.getLengthOfMousePress() >= 500) {
        showMachineMenu(machine);
        return;
    }
    setSelectedMachine(machine);
    if (onSelect) {
        onSelect(machine);
    }
}

void RackView::showAddMenu() {
    juce::PopupMenu menu;
    const auto types = MachineRegistry::getTypeNames();
    for (int i = 0; i < types.size(); i++) {
        if (types[i] != "TestTone") { // the placeholder machine stays out of the menu
            menu.addItem(i + 1, types[i]);
        }
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this, types](int result) {
        if (result > 0 && session.addMachine(types[result - 1]) >= 0) {
            setSelectedMachine(session.getNumMachines() - 1);
            if (onRackChanged) {
                onRackChanged();
            }
        }
    });
}

void RackView::showPatternMenu(int machine, int index) {
    juce::PopupMenu menu;
    PatternClipboard::addItems(menu, session, machine, index, [this] {
        repaint();
        if (onRackChanged) {
            onRackChanged();
        }
    });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
}

void RackView::showMachineMenu(int machine) {
    juce::PopupMenu menu;
    menu.addItem(1, "Remove " + session.getSong().machines[static_cast<size_t>(machine)].name);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this, machine](int result) {
        if (result == 1 && session.removeMachine(machine)) {
            setSelectedMachine(juce::jlimit(0, juce::jmax(0, session.getNumMachines() - 1), selected));
            if (onRackChanged) {
                onRackChanged();
            }
        }
    });
}
