#include "RackView.h"

#include "NatronLookAndFeel.h"
#include "PatternClipboard.h"
#include "Theme.h"
#include "core/MachineRegistry.h"

namespace {
    const juce::Colour background = NatronLookAndFeel::background;
    const juce::Colour keyTop { 0xffe3e5e8 };
    const juce::Colour keyBottom { 0xffaeb2b8 };
    const juce::Colour edge { 0xff3e4146 };
    const juce::Colour lit { 0xffffc04d };
    const juce::Colour ink = NatronLookAndFeel::ink;

    // A bevelled key: light grey when empty, the machine's colour when it holds notes.
    void drawKey(juce::Graphics& g, juce::Rectangle<float> key, juce::Colour fill, bool pressed) {
        const auto top = pressed ? fill.darker(0.15f) : fill.brighter(0.15f);
        const auto bottom = pressed ? fill.brighter(0.05f) : fill.darker(0.2f);
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillRoundedRectangle(key.translated(0.0f, 1.5f), 4.0f);
        g.setGradientFill(juce::ColourGradient(top, 0.0f, key.getY(), bottom, 0.0f, key.getBottom(), false));
        g.fillRoundedRectangle(key, 4.0f);
        g.setColour(edge);
        g.drawRoundedRectangle(key.reduced(0.5f), 4.0f, 1.0f);
    }
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
        // An empty rack bay with a "+" key.
        const auto add = addBounds().reduced(0, 6).toFloat();
        g.setColour(juce::Colour(0xff1f2023));
        g.fillRoundedRectangle(add, 6.0f);
        g.setColour(juce::Colour(0xff45484d));
        g.drawRoundedRectangle(add.reduced(0.5f), 6.0f, 1.0f);
        g.setColour(juce::Colour(0xffb9bdc3));
        g.setFont(NatronLookAndFeel::labelFont(14.0f));
        g.drawText("+  ADD MACHINE", add, juce::Justification::centred);
    }

    const bool blinkOn = (juce::Time::getMillisecondCounter() / 250) % 2 == 0;

    for (int m = 0; m < session.getNumMachines(); m++) {
        const auto& state = session.getSong().machines[static_cast<size_t>(m)];
        const auto row = rowBounds(m).toFloat();
        const auto colour = Theme::machineColour(state.type);
        const auto face = Theme::panelColour(state.type);
        NatronLookAndFeel::drawPanel(g, row, face);
        if (m == selected) {
            g.setColour(lit);
            g.drawRoundedRectangle(row.reduced(1.0f), 6.0f, 2.0f);
        }

        // Name plate: the machine's colour, its name in capitals and its type beneath.
        const auto plate = nameBounds(m).toFloat().reduced(6.0f, 6.0f);
        g.setGradientFill(juce::ColourGradient(colour.brighter(0.15f), 0.0f, plate.getY(), colour.darker(0.2f), 0.0f,
                                               plate.getBottom(), false));
        g.fillRoundedRectangle(plate, 4.0f);
        g.setColour(colour.darker(0.6f));
        g.drawRoundedRectangle(plate.reduced(0.5f), 4.0f, 1.0f);
        const auto plateText = colour.getPerceivedBrightness() > 0.55f ? ink : juce::Colours::white;
        g.setColour(plateText);
        g.setFont(NatronLookAndFeel::labelFont(14.0f));
        g.drawFittedText(state.name.toUpperCase(), plate.reduced(6.0f, 3.0f).withHeight(22.0f).toNearestInt(),
                         juce::Justification::centredLeft, 1);
        g.setColour(plateText.withAlpha(0.7f));
        g.setFont(NatronLookAndFeel::labelFont(10.0f));
        g.drawText(state.type.toUpperCase(), plate.reduced(6.0f, 3.0f).withTrimmedTop(21.0f), juce::Justification::centredLeft);

        const int current = m < engine.getNumMachines() ? engine.getCurrentPattern(m) : state.selectedPattern;
        const int queued = m < engine.getNumMachines() ? engine.getQueuedPattern(m) : -1;
        const int bank = visibleBank[static_cast<size_t>(m)];

        for (int b = 0; b < PatternBank::numBanks; b++) {
            const auto bounds = bankBounds(m, b).toFloat();
            drawKey(g, bounds, b == bank ? juce::Colour(0xff4a4e55) : keyTop, b == bank);
            g.setColour(b == bank ? lit : ink);
            g.setFont(NatronLookAndFeel::labelFont(13.0f));
            g.drawText(juce::String::charToString(static_cast<juce::juce_wchar>('A' + b)), bounds, juce::Justification::centred);
        }

        for (int slot = 0; slot < PatternBank::patternsPerBank; slot++) {
            const int index = PatternBank::indexOf(bank, slot);
            const auto bounds = padBounds(m, slot).toFloat();
            const bool hasNotes = !state.patterns[static_cast<size_t>(index)].isEmpty();
            const bool playing = index == current;

            drawKey(g, bounds, hasNotes ? colour.interpolatedWith(keyTop, 0.35f) : keyTop.interpolatedWith(keyBottom, 0.3f), playing);
            // An LED on each pad: lit while it plays, blinking while queued.
            const auto led = juce::Rectangle<float>(bounds.getCentreX() - 3.0f, bounds.getY() + 5.0f, 6.0f, 6.0f);
            const bool ledOn = playing || (index == queued && blinkOn);
            if (ledOn) {
                g.setColour(lit.withAlpha(0.35f));
                g.fillEllipse(led.expanded(3.0f));
            }
            g.setColour(ledOn ? lit : juce::Colour(0xff6b5a3a));
            g.fillEllipse(led);
            g.setColour(ink);
            g.setFont(NatronLookAndFeel::labelFont(12.0f));
            g.drawText(juce::String(slot + 1), bounds.withTrimmedTop(10.0f), juce::Justification::centred);
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
