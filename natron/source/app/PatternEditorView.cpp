#include "PatternEditorView.h"

#include "core/PatternEditing.h"
#include "core/dsp/DrumKit.h"
#include "core/machines/DrumMachine.h"

namespace {
    const juce::Colour background { 0xff1d1f21 };
    const juce::Colour lane { 0xff232528 };
    const juce::Colour blackKeyLane { 0xff1f2023 };
    const juce::Colour gridLine { 0xff2f3236 };
    const juce::Colour barLine { 0xff4a4f55 };
    const juce::Colour noteColour { 0xff81a2be };
    const juce::Colour accentColour { 0xffde935f };
    const juce::Colour text { 0xffc5c8c6 };

    constexpr int gutterWidth = 70;
    constexpr int lowestPitch = 24;  // C1
    constexpr int highestPitch = 96; // C7

    bool isBlackKey(int pitch) {
        const int n = pitch % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }
}

// The scrolling note area. Rows top to bottom are high to low pitches (or drum slots).
class PatternEditorView::Grid : public juce::Component {
public:
    explicit Grid(PatternEditorView& ownerToUse) :
        owner(ownerToUse) {}

    // Steps fill the visible width (fitWidth) at zoom 1; pinching changes the zoom.
    float zoomFactor = 1.0f;
    float fitWidth = 600.0f;
    int rowHeight = 22;

    float stepWidth() const {
        return juce::jlimit(8.0f, 160.0f, (fitWidth - gutterWidth) / static_cast<float>(numSteps()) * zoomFactor);
    }

    bool isDrums() const {
        return owner.machine < owner.session.getNumMachines()
               && owner.session.getSong().machines[static_cast<size_t>(owner.machine)].type == DrumMachine::typeName;
    }

    int numRows() const { return isDrums() ? DrumMachine::numSlots : highestPitch - lowestPitch + 1; }
    int pitchForRow(int row) const {
        return isDrums() ? DrumMachine::firstNote + (DrumMachine::numSlots - 1 - row) : highestPitch - row;
    }
    int rowForPitch(int pitch) const {
        return isDrums() ? DrumMachine::numSlots - 1 - (pitch - DrumMachine::firstNote) : highestPitch - pitch;
    }

    const Pattern& pattern() const {
        const auto& state = owner.session.getSong().machines[static_cast<size_t>(owner.machine)];
        return state.patterns[static_cast<size_t>(state.selectedPattern)];
    }
    int patternIndex() const {
        return owner.session.getSong().machines[static_cast<size_t>(owner.machine)].selectedPattern;
    }

    int numSteps() const { return pattern().bars * Transport::beatsPerBar * 4; }
    int idealWidth() const { return gutterWidth + juce::roundToInt(numSteps() * stepWidth()) + 1; }
    int idealHeight() const { return numRows() * rowHeight; }

    double beatAtX(float x) const { return (x - gutterWidth) / stepWidth() * Pattern::stepLength; }
    float xForBeat(double beat) const { return gutterWidth + static_cast<float>(beat / Pattern::stepLength) * stepWidth(); }

    juce::Rectangle<float> noteBounds(const Note& note) const {
        const int row = rowForPitch(note.pitch);
        return { xForBeat(note.start), static_cast<float>(row * rowHeight) + 1.0f,
                 juce::jmax(4.0f, static_cast<float>(note.length / Pattern::stepLength) * stepWidth() - 1.0f),
                 static_cast<float>(rowHeight) - 2.0f };
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(background);
        if (owner.machine >= owner.session.getNumMachines()) {
            return;
        }
        const int rows = numRows();
        const float right = xForBeat(pattern().lengthInBeats());

        for (int row = 0; row < rows; row++) {
            const int pitch = pitchForRow(row);
            const juce::Rectangle<float> laneBounds(gutterWidth, static_cast<float>(row * rowHeight),
                                                    right - gutterWidth, static_cast<float>(rowHeight));
            g.setColour(!isDrums() && isBlackKey(pitch) ? blackKeyLane : lane);
            g.fillRect(laneBounds);

            g.setColour(text.withAlpha(isDrums() || pitch % 12 == 0 ? 0.9f : 0.4f));
            g.setFont(juce::FontOptions(12.0f));
            const auto label = isDrums() ? juce::String(DrumKit::names()[static_cast<size_t>(pitch - DrumMachine::firstNote)])
                                         : juce::MidiMessage::getMidiNoteName(pitch, true, true, 4);
            g.drawText(label, juce::Rectangle<int>(4, row * rowHeight, gutterWidth - 8, rowHeight),
                       juce::Justification::centredLeft);
        }

        for (int step = 0; step <= numSteps(); step++) {
            g.setColour(step % 16 == 0 ? barLine : step % 4 == 0 ? gridLine.brighter(0.2f) : gridLine);
            g.drawVerticalLine(juce::roundToInt(gutterWidth + step * stepWidth()), 0.0f, static_cast<float>(rows * rowHeight));
        }

        for (const auto& note : pattern().notes) {
            if (note.start >= pattern().lengthInBeats() || rowForPitch(note.pitch) < 0 || rowForPitch(note.pitch) >= rows) {
                continue;
            }
            const auto bounds = noteBounds(note);
            const bool accent = note.velocity >= PatternEditing::accentVelocity - 1.0e-3f;
            g.setColour((accent ? accentColour : noteColour).withAlpha(0.45f + 0.55f * note.velocity));
            g.fillRoundedRectangle(bounds, 3.0f);
            g.setColour(background.withAlpha(0.6f));
            g.fillRect(bounds.withLeft(bounds.getRight() - 4.0f).reduced(0.0f, 4.0f)); // resize handle
        }

        // Playhead (position within the looping pattern).
        if (owner.engine.isPlaying()) {
            const double beat = std::fmod(owner.engine.getPlayheadBeat(), pattern().lengthInBeats());
            g.setColour(accentColour);
            g.drawVerticalLine(juce::roundToInt(xForBeat(beat)), 0.0f, static_cast<float>(rows * rowHeight));
        }
    }

    void mouseDown(const juce::MouseEvent& event) override {
        dragNote = -1;
        dragged = false;
        if (juce::Desktop::getInstance().getNumDraggingMouseSources() > 1) {
            startPinch();
            return;
        }
        const auto position = event.position;
        if (position.x < gutterWidth || owner.machine >= owner.session.getNumMachines()) {
            return;
        }
        const int pitch = pitchForRow(static_cast<int>(position.y) / rowHeight);
        const double beat = beatAtX(position.x);
        working = pattern();
        dragNote = PatternEditing::noteAt(working, beat, pitch);
        if (dragNote >= 0) {
            const auto bounds = noteBounds(working.notes[static_cast<size_t>(dragNote)]);
            resizing = position.x > bounds.getRight() - juce::jmax(8.0f, bounds.getWidth() * 0.25f);
            grabOffset = beat - working.notes[static_cast<size_t>(dragNote)].start;
        } else if (PatternEditing::add(working, PatternEditing::snap(beat, Pattern::stepLength), pitch) >= 0) {
            commit(); // a tap on an empty cell adds a note straight away
        }
    }

    void mouseDrag(const juce::MouseEvent& event) override {
        if (pinching || juce::Desktop::getInstance().getNumDraggingMouseSources() > 1) {
            updatePinch();
            return;
        }
        if (dragNote < 0 || event.getDistanceFromDragStart() < 6) {
            return;
        }
        dragged = true;
        const double beat = beatAtX(event.position.x);
        if (resizing) {
            const double start = working.notes[static_cast<size_t>(dragNote)].start;
            PatternEditing::resize(working, dragNote,
                                   PatternEditing::snap(beat - start, Pattern::stepLength) + Pattern::stepLength);
        } else {
            const int pitch = pitchForRow(juce::jlimit(0, numRows() - 1, static_cast<int>(event.position.y) / rowHeight));
            dragNote = PatternEditing::move(working, dragNote, PatternEditing::snap(beat - grabOffset + Pattern::stepLength / 2, Pattern::stepLength), pitch);
        }
        commit();
    }

    void mouseUp(const juce::MouseEvent& event) override {
        if (pinching) {
            pinching = juce::Desktop::getInstance().getNumDraggingMouseSources() > 1;
            return;
        }
        if (dragNote >= 0 && !dragged) {
            if (event.getLengthOfMousePress() >= 500) {
                PatternEditing::cycleVelocity(working, dragNote); // long press
            } else {
                PatternEditing::remove(working, dragNote); // tap on a note
            }
            commit();
        }
        dragNote = -1;
    }

    void mouseMagnify(const juce::MouseEvent&, float scale) override { zoom(scale); }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override {
        if (event.mods.isCtrlDown() || event.mods.isCommandDown()) {
            zoom(1.0f + wheel.deltaY);
        } else {
            Component::mouseWheelMove(event, wheel);
        }
    }

    void zoom(float factor) {
        zoomFactor = juce::jlimit(0.25f, 8.0f, zoomFactor * factor);
        owner.resized();
    }

private:
    PatternEditorView& owner;
    Pattern working;
    int dragNote = -1;
    bool resizing = false;
    bool dragged = false;
    double grabOffset = 0.0;
    bool pinching = false;
    float pinchStartDistance = 0.0f;
    float pinchStartZoom = 1.0f;

    void commit() {
        owner.session.setPattern(owner.machine, patternIndex(), working);
        repaint();
    }

    float fingerDistance() const {
        auto& desktop = juce::Desktop::getInstance();
        juce::Array<juce::Point<float>> points;
        for (auto& source : desktop.getMouseSources()) {
            if (source.isDragging()) {
                points.add(source.getScreenPosition());
            }
        }
        return points.size() >= 2 ? std::abs(points[0].x - points[1].x) : 0.0f;
    }

    void startPinch() {
        pinching = true;
        dragNote = -1;
        pinchStartDistance = juce::jmax(1.0f, fingerDistance());
        pinchStartZoom = zoomFactor;
    }

    void updatePinch() {
        if (!pinching) {
            startPinch();
        }
        const float distance = fingerDistance();
        if (distance > 0.0f) {
            zoomFactor = juce::jlimit(0.25f, 8.0f, pinchStartZoom * distance / pinchStartDistance);
            owner.resized();
        }
    }
};

PatternEditorView::PatternEditorView(Session& sessionToUse, const Engine& engineToShow, juce::MidiKeyboardState& keyboardState) :
    session(sessionToUse), engine(engineToShow), automation(sessionToUse, AutomationEditor::Mode::pattern),
    keyboard(keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard) {
    addAndMakeVisible(automation);
    keyboard.setAvailableRange(24, 108);
    keyboard.setLowestVisibleKey(48);
    keyboard.setKeyWidth(28.0f);
    addAndMakeVisible(keyboard);
    title.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    addAndMakeVisible(title);

    for (const int bars : { 1, 2, 4, 8 }) {
        lengthBox.addItem(juce::String(bars) + (bars == 1 ? " bar" : " bars"), bars);
    }
    lengthBox.onChange = [this] {
        if (machine >= session.getNumMachines()) {
            return;
        }
        const auto& state = session.getSong().machines[static_cast<size_t>(machine)];
        Pattern pattern = state.patterns[static_cast<size_t>(state.selectedPattern)];
        if (PatternEditing::setBars(pattern, lengthBox.getSelectedId())) {
            session.setPattern(machine, state.selectedPattern, pattern);
            refresh();
        }
    };
    addAndMakeVisible(lengthBox);

    grid = std::make_unique<Grid>(*this);
    viewport.setViewedComponent(grid.get(), false);
    addAndMakeVisible(viewport);
    startTimerHz(30);
    refresh();
}

void PatternEditorView::setMachine(int newMachine) {
    machine = newMachine;
    automation.setMachine(newMachine);
    refresh();
    // Scroll the keyboard to the pattern's notes (or around middle C when it's empty).
    if (!grid->isDrums() && machine < session.getNumMachines()) {
        int highest = 66;
        for (const auto& note : grid->pattern().notes) {
            highest = note.pitch > highest || highest == 66 ? note.pitch : highest;
        }
        viewport.setViewPosition(0, juce::jmax(0, grid->rowForPitch(highest + 4) * grid->rowHeight));
    }
}

void PatternEditorView::refresh() {
    if (machine >= session.getNumMachines()) {
        title.setText("No machine", juce::dontSendNotification);
        return;
    }
    const auto& state = session.getSong().machines[static_cast<size_t>(machine)];
    grid->rowHeight = grid->isDrums() ? 44 : 22; // drum rows are few: make them easy to hit
    title.setText(state.name + "  " + PatternBank::nameOf(state.selectedPattern), juce::dontSendNotification);
    lengthBox.setSelectedId(state.patterns[static_cast<size_t>(state.selectedPattern)].bars, juce::dontSendNotification);
    automation.refresh();
    resized();
    grid->repaint();
}

void PatternEditorView::paint(juce::Graphics& g) {
    g.fillAll(background);
}

void PatternEditorView::resized() {
    auto area = getLocalBounds();
    auto header = area.removeFromTop(40).reduced(4);
    lengthBox.setBounds(header.removeFromRight(110));
    title.setBounds(header);
    keyboard.setBounds(area.removeFromBottom(64));
    area.removeFromBottom(4);
    automation.setBounds(area.removeFromBottom(juce::jmin(150, area.getHeight() / 3)));
    area.removeFromBottom(4);
    viewport.setBounds(area);
    grid->fitWidth = static_cast<float>(viewport.getMaximumVisibleWidth());
    grid->setSize(juce::jmax(viewport.getMaximumVisibleWidth(), grid->idealWidth()), grid->idealHeight());
}

void PatternEditorView::timerCallback() {
    if (engine.isPlaying() && isShowing()) {
        grid->repaint();
    }
}
