#include "AutomationEditor.h"

namespace {
    const juce::Colour background { 0xff232528 };
    const juce::Colour gridLine { 0xff34373b };
    const juce::Colour laneColour { 0xffb5bd68 };
    const juce::Colour text { 0xffc5c8c6 };
    constexpr float pointRadius = 6.0f;
}

class AutomationEditor::LaneArea : public juce::Component {
public:
    explicit LaneArea(AutomationEditor& ownerToUse) :
        owner(ownerToUse) {}

    void paint(juce::Graphics& g) override {
        g.fillAll(background);
        const auto* choice = owner.currentChoice();
        const double length = owner.lengthInBeats();
        for (int beat = 0; beat <= static_cast<int>(length); beat++) {
            g.setColour(beat % 4 == 0 ? gridLine.brighter(0.3f) : gridLine);
            g.drawVerticalLine(juce::roundToInt(xFor(beat)), 0.0f, static_cast<float>(getHeight()));
        }
        if (choice == nullptr) {
            g.setColour(text.withAlpha(0.6f));
            g.drawText("Choose a parameter to automate", getLocalBounds(), juce::Justification::centred);
            return;
        }
        const auto lane = owner.currentLane();
        if (lane.points.empty()) {
            g.setColour(text.withAlpha(0.6f));
            g.drawText("Tap to add points for " + choice->label, getLocalBounds(), juce::Justification::centred);
            return;
        }
        juce::Path path;
        path.startNewSubPath(0.0f, yFor(lane.points.front().value, choice->spec));
        for (const auto& point : lane.points) {
            path.lineTo(xFor(point.beat), yFor(point.value, choice->spec));
        }
        path.lineTo(static_cast<float>(getWidth()), yFor(lane.points.back().value, choice->spec));
        g.setColour(laneColour);
        g.strokePath(path, juce::PathStrokeType(2.0f));
        for (const auto& point : lane.points) {
            g.fillEllipse(xFor(point.beat) - pointRadius, yFor(point.value, choice->spec) - pointRadius, 2 * pointRadius, 2 * pointRadius);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override {
        owner.session.beginGesture(); // the whole press, drag and release is one undo step
        const auto* choice = owner.currentChoice();
        if (choice == nullptr) {
            return;
        }
        working = owner.currentLane();
        dragIndex = pointAt(event.position, choice->spec);
        moved = false;
        if (dragIndex >= 0 && event.mods.isPopupMenu()) {
            working.points.erase(working.points.begin() + dragIndex);
            owner.commit(working);
            dragIndex = -1;
            return;
        }
        if (dragIndex < 0) {
            working.setPoint(snap(beatFor(event.position.x)), valueFor(event.position.y, choice->spec));
            owner.commit(working);
            dragIndex = pointAt(event.position, choice->spec);
            moved = true; // a new point isn't a long-press candidate
        }
    }

    void mouseDrag(const juce::MouseEvent& event) override {
        const auto* choice = owner.currentChoice();
        if (choice == nullptr || dragIndex < 0 || event.getDistanceFromDragStart() < 4) {
            return;
        }
        moved = true;
        working.points.erase(working.points.begin() + dragIndex);
        const double beat = juce::jlimit(0.0, owner.lengthInBeats(), snap(beatFor(event.position.x)));
        working.setPoint(beat, valueFor(event.position.y, choice->spec));
        owner.commit(working);
        dragIndex = -1;
        for (size_t i = 0; i < working.points.size(); i++) {
            if (std::abs(working.points[i].beat - beat) < 1.0e-9) {
                dragIndex = static_cast<int>(i);
            }
        }
    }

    void mouseUp(const juce::MouseEvent& event) override {
        const juce::ScopeGuard endGesture { [this] { owner.session.endGesture(); } };
        if (dragIndex >= 0 && !moved && event.getLengthOfMousePress() >= 500) {
            working.points.erase(working.points.begin() + dragIndex); // long press deletes
            owner.commit(working);
        }
        dragIndex = -1;
    }

private:
    AutomationEditor& owner;
    AutomationLane working;
    int dragIndex = -1;
    bool moved = false;

    float xFor(double beat) const { return static_cast<float>(beat / owner.lengthInBeats() * getWidth()); }
    double beatFor(float x) const { return x / static_cast<double>(juce::jmax(1, getWidth())) * owner.lengthInBeats(); }
    double snap(double beat) const { return std::round(beat / owner.gridInBeats()) * owner.gridInBeats(); }

    float yFor(float value, const ParameterSpec& spec) const {
        const float normalised = (value - spec.minValue) / juce::jmax(1.0e-9f, spec.maxValue - spec.minValue);
        return (1.0f - normalised) * (getHeight() - 2 * pointRadius) + pointRadius;
    }
    float valueFor(float y, const ParameterSpec& spec) const {
        const float normalised = 1.0f - (y - pointRadius) / juce::jmax(1.0f, getHeight() - 2 * pointRadius);
        float value = spec.minValue + juce::jlimit(0.0f, 1.0f, normalised) * (spec.maxValue - spec.minValue);
        if (spec.interval > 0.0f) {
            value = spec.minValue + std::round((value - spec.minValue) / spec.interval) * spec.interval;
        }
        return value;
    }

    int pointAt(juce::Point<float> position, const ParameterSpec& spec) const {
        const auto lane = owner.currentLane();
        for (size_t i = 0; i < lane.points.size(); i++) {
            const juce::Point<float> p(xFor(lane.points[i].beat), yFor(lane.points[i].value, spec));
            if (p.getDistanceFrom(position) <= pointRadius * 2.0f) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};

AutomationEditor::AutomationEditor(Session& sessionToUse, Mode modeToUse) :
    session(sessionToUse), mode(modeToUse) {
    title.setText(mode == Mode::pattern ? "Pattern automation" : "Song automation", juce::dontSendNotification);
    title.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    addAndMakeVisible(title);
    targetBox.setTextWhenNothingSelected("Parameter...");
    targetBox.onChange = [this] { lane->repaint(); };
    addAndMakeVisible(targetBox);
    clearButton.onClick = [this] {
        if (currentChoice() != nullptr) {
            AutomationLane empty = currentLane();
            empty.points.clear();
            commit(empty);
        }
    };
    addAndMakeVisible(clearButton);
    lane = std::make_unique<LaneArea>(*this);
    addAndMakeVisible(*lane);
    refresh();
}

AutomationEditor::~AutomationEditor() = default;

void AutomationEditor::setMachine(int newMachine) {
    machine = newMachine;
    refresh();
}

void AutomationEditor::refresh() {
    const auto previous = targetBox.getText();
    rebuildChoices();
    for (int i = 0; i < static_cast<int>(choices.size()); i++) {
        if (choices[static_cast<size_t>(i)].label == previous) {
            targetBox.setSelectedId(i + 1, juce::dontSendNotification);
        }
    }
    lane->repaint();
}

void AutomationEditor::rebuildChoices() {
    choices.clear();
    targetBox.clear(juce::dontSendNotification);
    auto* root = targetBox.getRootMenu();

    auto addSet = [this](juce::PopupMenu& menu, const ParameterSet* parameters, AutomationTarget target, const juce::String& prefix) {
        if (parameters == nullptr) {
            return;
        }
        for (int p = 0; p < parameters->size(); p++) {
            target.parameterId = parameters->getSpec(p).id;
            choices.push_back({ prefix + parameters->getSpec(p).name, target, parameters->getSpec(p) });
            menu.addItem(static_cast<int>(choices.size()), parameters->getSpec(p).name);
        }
    };
    auto addMachine = [&](juce::PopupMenu& into, int m, const juce::String& prefix) {
        const auto& state = session.getSong().machines[static_cast<size_t>(m)];
        AutomationTarget target;
        target.machine = m;
        juce::PopupMenu machineMenu, mixerMenu;
        target.scope = AutomationTarget::Scope::machine;
        addSet(machineMenu, session.getParameters(m), target, prefix);
        into.addSubMenu(state.type, machineMenu);
        target.scope = AutomationTarget::Scope::mixer;
        addSet(mixerMenu, session.getMixerParameters(m), target, prefix + "Mixer ");
        into.addSubMenu("Mixer", mixerMenu);
        for (int e = 0; e < Engine::effectsPerMachine; e++) {
            if (const auto* effect = session.getEffectParameters(m, e)) {
                juce::PopupMenu effectMenu;
                target.scope = AutomationTarget::Scope::effect;
                target.effectSlot = e;
                addSet(effectMenu, effect, target, prefix + state.effects[static_cast<size_t>(e)].type + " ");
                into.addSubMenu("Effect " + juce::String(e + 1) + ": " + state.effects[static_cast<size_t>(e)].type, effectMenu);
            }
        }
    };

    if (mode == Mode::pattern) {
        if (machine < session.getNumMachines()) {
            addMachine(*root, machine, {});
        }
    } else {
        for (int m = 0; m < session.getNumMachines(); m++) {
            juce::PopupMenu machineMenu;
            const auto& name = session.getSong().machines[static_cast<size_t>(m)].name;
            addMachine(machineMenu, m, name + " ");
            root->addSubMenu(name, machineMenu);
        }
        juce::PopupMenu masterMenu;
        AutomationTarget master;
        master.scope = AutomationTarget::Scope::master;
        addSet(masterMenu, &session.getMasterParameters(), master, "Master ");
        root->addSubMenu("Master", masterMenu);
    }
}

const AutomationEditor::Choice* AutomationEditor::currentChoice() const {
    const int id = targetBox.getSelectedId();
    return id > 0 && id <= static_cast<int>(choices.size()) ? &choices[static_cast<size_t>(id - 1)] : nullptr;
}

double AutomationEditor::lengthInBeats() const {
    if (mode == Mode::pattern) {
        if (machine >= session.getNumMachines()) {
            return 4.0;
        }
        const auto& state = session.getSong().machines[static_cast<size_t>(machine)];
        return state.patterns[static_cast<size_t>(state.selectedPattern)].lengthInBeats();
    }
    return juce::jmax(16, session.getSong().endBar()) * static_cast<double>(Transport::beatsPerBar);
}

AutomationLane AutomationEditor::currentLane() const {
    const auto* choice = currentChoice();
    AutomationLane result;
    if (choice == nullptr) {
        return result;
    }
    result.target = choice->target;
    const AutomationLanes* lanes = nullptr;
    if (mode == Mode::pattern) {
        const auto& state = session.getSong().machines[static_cast<size_t>(machine)];
        lanes = &state.patterns[static_cast<size_t>(state.selectedPattern)].automation;
        result.target.machine = 0; // pattern lanes don't name a machine
    } else {
        lanes = &session.getSongAutomation();
    }
    for (const auto& existing : *lanes) {
        if (existing.target == result.target) {
            return existing;
        }
    }
    return result;
}

void AutomationEditor::commit(const AutomationLane& edited) {
    auto replace = [&](AutomationLanes lanes) {
        lanes.erase(std::remove_if(lanes.begin(), lanes.end(), [&](const AutomationLane& l) { return l.target == edited.target; }),
                    lanes.end());
        if (!edited.points.empty()) {
            lanes.push_back(edited);
        }
        return lanes;
    };
    if (mode == Mode::pattern) {
        const auto& state = session.getSong().machines[static_cast<size_t>(machine)];
        Pattern pattern = state.patterns[static_cast<size_t>(state.selectedPattern)];
        pattern.automation = replace(pattern.automation);
        session.setPattern(machine, state.selectedPattern, pattern);
    } else {
        session.setSongAutomation(replace(session.getSongAutomation()));
    }
    lane->repaint();
}

void AutomationEditor::resized() {
    auto area = getLocalBounds();
    auto header = area.removeFromTop(32);
    title.setBounds(header.removeFromLeft(150));
    clearButton.setBounds(header.removeFromRight(70).reduced(2));
    targetBox.setBounds(header.removeFromLeft(260).reduced(2));
    lane->setBounds(area.reduced(0, 2));
}
