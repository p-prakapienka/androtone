#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <set>
#include <vector>

#include "../PluginProcessor.h"
#include "CliptoneLookAndFeel.h"

// Piano-roll editor for one MIDI clip.
//
// Layout: a ruler on top (bars / beats, the loop brace or start/end markers), a keyboard strip on
// the left, the note grid, and a velocity lane at the bottom.
//
// Touch-first gestures: tap empty grid = add a note one grid cell long (drag to set its length);
// drag a note = move it (time + pitch, snapping keeps the note's offset from the grid); drag its
// right edge = resize; double-tap a note = delete; drag in the velocity lane = set velocity; drag
// on the keyboard = scroll pitch; drag the brace edges/body = change / move the loop.
//
// Edits preview live (the audio thread hears them immediately) and are committed as one undo step
// per gesture.
class NoteGrid : public juce::Component, private juce::Timer {
public:
    explicit NoteGrid(CliptoneAudioProcessor& p) : processorRef(p) {
        startTimerHz(30);
    }

    std::function<void()> onSelectionChanged;

    void setClip(int newTrack, int newClip) {
        trackIndex = newTrack;
        clipIndex = newClip;
        selection.clear();
        reload();
        centreOnNotes();
        repaint();
    }

    // Re-reads the clip from the processor (after undo/redo or a toolbar edit).
    void reload() {
        data = processorRef.getClipData(trackIndex, clipIndex);
        if (data != nullptr) {
            notes = data->notes;
        } else {
            notes.clear();
        }
        std::set<int> valid;
        for (int index : selection) {
            if (index < static_cast<int>(notes.size())) {
                valid.insert(index);
            }
        }
        selection = valid;
        repaint();
    }

    bool hasClip() const { return data != nullptr; }
    ClipData::Ptr getShownData() const { return data; }
    int getTrack() const { return trackIndex; }
    int getClipIndex() const { return clipIndex; }

    void setGrid(double beats) { grid = beats; repaint(); }
    double getGrid() const { return grid; }
    void setSnap(bool shouldSnap) { snap = shouldSnap; }
    void setFold(bool shouldFold) { fold = shouldFold; repaint(); }

    const std::set<int>& getSelection() const { return selection; }

    // Indices the toolbar operates on: the selection, or every note if nothing is selected.
    std::vector<int> getTargetIndices() const {
        std::vector<int> result;
        if (selection.empty()) {
            for (int i = 0; i < static_cast<int>(notes.size()); i++) {
                result.push_back(i);
            }
        } else {
            result.assign(selection.begin(), selection.end());
        }
        return result;
    }

    // Applies `edit` to a copy of the clip as one undoable step, then reloads.
    void commitEdit(const juce::String& name, const std::function<void(ClipData&)>& edit) {
        if (data == nullptr) {
            return;
        }
        auto before = data;
        auto after = data->copy();
        after->notes = notes;
        edit(*after);
        const auto selected = selectedNotes(after->notes);
        after->sanitise();
        processorRef.performClipEdit(trackIndex, clipIndex, before, after, name);
        reload();
        reselect(selected);
    }

    void paint(juce::Graphics& g) override {
        const juce::Colour background { CliptoneLookAndFeel::Palette::BACKGROUND };
        const juce::Colour surface { CliptoneLookAndFeel::Palette::SURFACE };
        const juce::Colour outline { CliptoneLookAndFeel::Palette::OUTLINE };
        const juce::Colour muted { CliptoneLookAndFeel::Palette::TEXT_MUTED };
        const juce::Colour accent { CliptoneLookAndFeel::Palette::ACCENT };

        g.fillAll(background);
        if (data == nullptr) {
            g.setColour(muted);
            g.drawFittedText("No clip selected.\nTurn on the pencil in the Session tab and tap a clip,\n"
                             "or double-tap an empty slot to create one.",
                             getLocalBounds(), juce::Justification::centred, 3);
            return;
        }

        const auto gridArea = getGridArea();
        const auto rows = getVisibleRows();
        const float rowH = getRowHeight();
        const juce::Colour trackColour = processorRef.getClipColour(trackIndex, clipIndex);

        // Rows: black keys darker; C rows outlined.
        for (int r = 0; r < static_cast<int>(rows.size()); r++) {
            const int pitch = rows[static_cast<size_t>(r)];
            const float y = gridArea.getBottom() - (r + 1) * rowH;
            const bool black = juce::MidiMessage::isMidiNoteBlack(pitch);
            g.setColour(black ? background : surface);
            g.fillRect(juce::Rectangle<float>(gridArea.getX(), y, gridArea.getWidth(), rowH));
            if (pitch % 12 == 0) {
                g.setColour(outline);
                g.drawHorizontalLine(static_cast<int>(y + rowH), gridArea.getX(), gridArea.getRight());
            }

            // Keyboard strip.
            auto key = juce::Rectangle<float>(0.0f, y, static_cast<float>(KEYS_WIDTH) - 2.0f, rowH);
            g.setColour(black ? juce::Colour(0xFF1A2629) : juce::Colour(0xFF9FB3B6));
            g.fillRect(key.reduced(0.0f, 0.5f));
            if (pitch % 12 == 0 || fold) {
                g.setColour(black ? muted : background);
                g.setFont(juce::FontOptions(juce::jmin(11.0f, rowH)));
                g.drawText(juce::MidiMessage::getMidiNoteName(pitch, true, true, 3), key.reduced(3.0f, 0.0f),
                           juce::Justification::centredLeft);
            }
        }

        // Region outside the playable range is dimmed.
        const double playEnd = data->getPlayEnd();
        const double playStart = data->looping ? juce::jmin(data->startMarker, data->loopStart) : data->startMarker;
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillRect(juce::Rectangle<float>(gridArea.getX(), gridArea.getY(), beatToX(playStart) - gridArea.getX(), gridArea.getHeight()));
        g.fillRect(juce::Rectangle<float>(beatToX(playEnd), gridArea.getY(), gridArea.getRight() - beatToX(playEnd), gridArea.getHeight()));

        // Vertical grid lines: grid cells faint, beats stronger, bars strongest.
        for (double beat = std::ceil(viewStart / grid) * grid; beat <= viewStart + viewLength + 1e-9; beat += grid) {
            const float x = beatToX(beat);
            const bool bar = std::fmod(beat + 1e-9, Clip::BEATS_PER_BAR) < 2e-9;
            const bool wholeBeat = std::fmod(beat + 1e-9, 1.0) < 2e-9;
            g.setColour(outline.withAlpha(bar ? 1.0f : (wholeBeat ? 0.7f : 0.3f)));
            g.drawVerticalLine(static_cast<int>(x), gridArea.getY(), gridArea.getBottom() + VELOCITY_HEIGHT);
        }

        // Notes, shaded by velocity.
        for (int i = 0; i < static_cast<int>(notes.size()); i++) {
            const auto bounds = noteBounds(i);
            if (bounds.isEmpty()) {
                continue;
            }
            const auto& note = notes[static_cast<size_t>(i)];
            const float strength = 0.35f + 0.65f * (note.velocity / 127.0f);
            juce::Colour fill = trackColour.interpolatedWith(background, 1.0f - strength);
            if (note.muted) {
                fill = muted.withAlpha(0.35f);
            }
            g.setColour(fill);
            g.fillRoundedRectangle(bounds, 2.0f);
            g.setColour(selection.count(i) > 0 ? juce::Colours::white : fill.darker(0.6f));
            g.drawRoundedRectangle(bounds, 2.0f, selection.count(i) > 0 ? 1.5f : 1.0f);
        }

        // Velocity lane.
        const auto velocityArea = getVelocityArea();
        g.setColour(surface);
        g.fillRect(velocityArea);
        g.setColour(outline);
        g.drawHorizontalLine(static_cast<int>(velocityArea.getY()), 0.0f, static_cast<float>(getWidth()));
        g.setColour(muted);
        g.setFont(juce::FontOptions(10.0f));
        g.drawText("Vel", juce::Rectangle<float>(0.0f, velocityArea.getY(), KEYS_WIDTH, 14.0f), juce::Justification::centred);
        for (int i = 0; i < static_cast<int>(notes.size()); i++) {
            const auto& note = notes[static_cast<size_t>(i)];
            const float x = beatToX(note.start);
            if (x < gridArea.getX() || x > gridArea.getRight()) {
                continue;
            }
            const float h = (velocityArea.getHeight() - 6.0f) * note.velocity / 127.0f;
            g.setColour(selection.count(i) > 0 ? juce::Colours::white : trackColour);
            g.fillRect(juce::Rectangle<float>(x, velocityArea.getBottom() - h, 3.0f, h));
            g.fillEllipse(x - 2.0f, velocityArea.getBottom() - h - 3.0f, 7.0f, 7.0f);
        }

        // Ruler with bar numbers and the loop brace (or start/end markers when not looping).
        const auto ruler = getRulerArea();
        g.setColour(surface);
        g.fillRect(ruler);
        g.setColour(muted);
        g.setFont(juce::FontOptions(11.0f));
        for (double bar = std::floor(viewStart / 4.0) * 4.0; bar < viewStart + viewLength; bar += 4.0) {
            if (bar >= viewStart) {
                g.drawText(juce::String(static_cast<int>(bar / 4.0) + 1),
                           juce::Rectangle<float>(beatToX(bar) + 2.0f, ruler.getY(), 30.0f, 12.0f),
                           juce::Justification::centredLeft);
            }
        }
        const auto brace = braceBounds();
        g.setColour(data->looping ? accent : muted);
        g.fillRect(brace);
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.fillRect(brace.withWidth(3.0f));
        g.fillRect(brace.withX(brace.getRight() - 3.0f).withWidth(3.0f));
        if (data->looping && data->startMarker < data->loopStart) {
            g.setColour(juce::Colours::white);
            const float sx = beatToX(data->startMarker);
            juce::Path marker;
            marker.addTriangle(sx, ruler.getY() + 12.0f, sx + 7.0f, ruler.getY() + 17.0f, sx, ruler.getBottom());
            g.fillPath(marker);
        }

        // Playhead.
        if (processorRef.isPlaying() && processorRef.getPlayingClip(trackIndex) == clipIndex) {
            const float x = beatToX(processorRef.getClipPlayPosition(trackIndex));
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.drawVerticalLine(static_cast<int>(x), ruler.getY(), velocityArea.getBottom());
        }
    }

    void resized() override {
        fitView();
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override {
        if (e.mods.isCommandDown() || e.mods.isCtrlDown()) {
            zoomBy(wheel.deltaY > 0 ? 0.8 : 1.25, xToBeat(static_cast<float>(e.x)));
        } else if (std::abs(wheel.deltaX) > std::abs(wheel.deltaY)) {
            scrollTimeBy(-wheel.deltaX * viewLength * 0.5);
        } else {
            scrollPitchBy(wheel.deltaY > 0 ? 2 : -2);
        }
    }

    void zoomBy(double factor, double anchorBeat) {
        const double extent = getExtentToShow();
        const double newLength = juce::jlimit(1.0, juce::jmax(extent, 4.0), viewLength * factor);
        viewStart = juce::jlimit(0.0, juce::jmax(0.0, extent - newLength),
                                 anchorBeat - (anchorBeat - viewStart) * (newLength / viewLength));
        viewLength = newLength;
        repaint();
    }

    void scrollTimeBy(double beats) {
        viewStart = juce::jlimit(0.0, juce::jmax(0.0, getExtentToShow() - viewLength), viewStart + beats);
        repaint();
    }

    void fitView() {
        viewStart = 0.0;
        viewLength = getExtentToShow();
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (data == nullptr) {
            return;
        }
        dragMode = DragMode::None;
        dragStartData = data;
        dragStartNotes = notes;
        mouseDownPos = e.position;
        changed = false;

        if (getRulerArea().contains(e.position)) {
            beginBraceDrag(e.position);
            return;
        }
        if (e.position.x < KEYS_WIDTH && !getVelocityArea().contains(e.position)) {
            dragMode = DragMode::ScrollPitch;
            dragStartLowest = lowestPitch;
            return;
        }
        if (getVelocityArea().contains(e.position)) {
            dragMode = DragMode::Velocity;
            setVelocityAt(e.position);
            return;
        }

        const int hit = hitTestNote(e.position);
        if (hit >= 0) {
            if (!e.mods.isShiftDown() && selection.count(hit) == 0) {
                selection.clear();
            }
            selection.insert(hit);
            const auto bounds = noteBounds(hit);
            dragMode = e.position.x > bounds.getRight() - RESIZE_HANDLE ? DragMode::Resize : DragMode::Move;
            notifySelection();
            repaint();
            return;
        }

        // Empty grid: add a note one grid cell long; dragging sets its length.
        const int pitch = yToPitch(e.position.y);
        if (pitch < 0) {
            return;
        }
        double start = xToBeat(e.position.x);
        start = snap ? std::floor(start / grid) * grid : start;
        if (start < 0.0 || start >= data->getExtent()) {
            return;
        }
        MidiNote note { start, grid, pitch, lastVelocity, false };
        notes.push_back(note);
        const int added = static_cast<int>(notes.size()) - 1;
        resolveOverlaps(added);
        selection = { indexOf(note) };
        dragMode = DragMode::Resize;
        dragStartNotes = notes;
        changed = true;
        preview();
        notifySelection();
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (data == nullptr || dragMode == DragMode::None) {
            return;
        }
        const auto delta = e.position - mouseDownPos;

        switch (dragMode) {
            case DragMode::ScrollPitch: {
                const int rowsMoved = static_cast<int>(delta.y / getRowHeight());
                lowestPitch = juce::jlimit(0, 127 - getNumRows() + 1, dragStartLowest + rowsMoved);
                repaint();
                return;
            }
            case DragMode::Velocity:
                setVelocityAt(e.position);
                return;
            case DragMode::Brace:
            case DragMode::BraceStart:
            case DragMode::BraceEnd:
                dragBrace(delta.x);
                return;
            case DragMode::Move:
                moveSelection(delta);
                return;
            case DragMode::Resize:
                resizeSelection(delta.x);
                return;
            case DragMode::None:
                return;
        }
    }

    void mouseUp(const juce::MouseEvent&) override {
        if (data == nullptr) {
            return;
        }
        if (changed) {
            const auto selected = selectedNotes(notes);
            auto after = (braceData != nullptr ? braceData : data)->copy();
            after->notes = notes;
            after->sanitise();
            processorRef.performClipEdit(trackIndex, clipIndex, dragStartData, after, gestureName());
            reload();
            reselect(selected);
        }
        braceData = nullptr;
        dragMode = DragMode::None;
        changed = false;
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override {
        const int hit = hitTestNote(e.position);
        if (hit < 0 || data == nullptr) {
            return;
        }
        selection.clear();
        notes.erase(notes.begin() + hit);
        auto after = data->copy();
        after->notes = notes;
        after->sanitise();
        processorRef.performClipEdit(trackIndex, clipIndex, dragStartData != nullptr ? dragStartData : data, after, "Delete note");
        reload();
        notifySelection();
    }

    void selectAll() {
        selection.clear();
        for (int i = 0; i < static_cast<int>(notes.size()); i++) {
            selection.insert(i);
        }
        notifySelection();
        repaint();
    }

    void clearSelection() {
        selection.clear();
        notifySelection();
        repaint();
    }

private:
    enum class DragMode { None, Move, Resize, Velocity, ScrollPitch, Brace, BraceStart, BraceEnd };

    static constexpr int KEYS_WIDTH = 40;
    static constexpr int RULER_HEIGHT = 22;
    static constexpr int VELOCITY_HEIGHT = 60;
    static constexpr float RESIZE_HANDLE = 8.0f;

    CliptoneAudioProcessor& processorRef;
    int trackIndex = -1;
    int clipIndex = -1;
    ClipData::Ptr data;
    std::vector<MidiNote> notes; // working copy (unsorted during a drag)
    std::set<int> selection;

    double grid = 0.25;
    bool snap = true;
    bool fold = false;
    double viewStart = 0.0;
    double viewLength = 4.0;
    int lowestPitch = 48;
    int lastVelocity = 100;

    DragMode dragMode = DragMode::None;
    ClipData::Ptr dragStartData;
    ClipData::Ptr braceData;
    std::vector<MidiNote> dragStartNotes;
    juce::Point<float> mouseDownPos;
    int dragStartLowest = 48;
    bool changed = false;

    void timerCallback() override {
        if (data != nullptr && processorRef.isPlaying() && processorRef.getPlayingClip(trackIndex) == clipIndex) {
            repaint();
        }
    }

    void notifySelection() {
        if (onSelectionChanged) {
            onSelectionChanged();
        }
    }

    juce::String gestureName() const {
        switch (dragMode) {
            case DragMode::Move: return "Move notes";
            case DragMode::Resize: return "Draw / resize note";
            case DragMode::Velocity: return "Change velocity";
            case DragMode::Brace:
            case DragMode::BraceStart:
            case DragMode::BraceEnd: return "Change loop";
            default: return "Edit clip";
        }
    }

    // ---- geometry ----

    juce::Rectangle<float> getRulerArea() const {
        return juce::Rectangle<float>(KEYS_WIDTH, 0.0f, static_cast<float>(getWidth() - KEYS_WIDTH), RULER_HEIGHT);
    }

    juce::Rectangle<float> getVelocityArea() const {
        return juce::Rectangle<float>(0.0f, static_cast<float>(getHeight() - VELOCITY_HEIGHT),
                                      static_cast<float>(getWidth()), VELOCITY_HEIGHT);
    }

    juce::Rectangle<float> getGridArea() const {
        return juce::Rectangle<float>(KEYS_WIDTH, RULER_HEIGHT, static_cast<float>(getWidth() - KEYS_WIDTH),
                                      static_cast<float>(getHeight() - RULER_HEIGHT - VELOCITY_HEIGHT));
    }

    double getExtentToShow() const {
        if (data == nullptr) {
            return 4.0;
        }
        return juce::jmax(4.0, std::ceil(data->getExtent() / Clip::BEATS_PER_BAR) * Clip::BEATS_PER_BAR);
    }

    float beatToX(double beat) const {
        const auto area = getGridArea();
        return area.getX() + static_cast<float>((beat - viewStart) / viewLength) * area.getWidth();
    }

    double xToBeat(float x) const {
        const auto area = getGridArea();
        return viewStart + (x - area.getX()) / area.getWidth() * viewLength;
    }

    float getRowHeight() const {
        if (fold) {
            const int rows = juce::jmax(1, static_cast<int>(foldedPitches().size()));
            return juce::jlimit(10.0f, 28.0f, getGridArea().getHeight() / rows);
        }
        return 14.0f;
    }

    int getNumRows() const {
        return juce::jmax(1, static_cast<int>(getGridArea().getHeight() / getRowHeight()));
    }

    std::vector<int> foldedPitches() const {
        std::set<int> used;
        for (const auto& note : notes) {
            used.insert(note.pitch);
        }
        return { used.begin(), used.end() };
    }

    // Pitches of the visible rows, bottom to top.
    std::vector<int> getVisibleRows() const {
        if (fold) {
            return foldedPitches();
        }
        std::vector<int> rows;
        for (int r = 0; r < getNumRows() && lowestPitch + r <= 127; r++) {
            rows.push_back(lowestPitch + r);
        }
        return rows;
    }

    int yToPitch(float y) const {
        const auto area = getGridArea();
        const int row = static_cast<int>((area.getBottom() - y) / getRowHeight());
        const auto rows = getVisibleRows();
        return row >= 0 && row < static_cast<int>(rows.size()) ? rows[static_cast<size_t>(row)] : -1;
    }

    float pitchToY(int pitch) const {
        const auto rows = getVisibleRows();
        for (int r = 0; r < static_cast<int>(rows.size()); r++) {
            if (rows[static_cast<size_t>(r)] == pitch) {
                return getGridArea().getBottom() - (r + 1) * getRowHeight();
            }
        }
        return -1.0f;
    }

    juce::Rectangle<float> noteBounds(int index) const {
        const auto& note = notes[static_cast<size_t>(index)];
        const float y = pitchToY(note.pitch);
        if (y < 0.0f) {
            return {};
        }
        const auto area = getGridArea();
        const float x1 = juce::jmax(area.getX(), beatToX(note.start));
        const float x2 = juce::jmin(area.getRight(), beatToX(note.end()));
        if (x2 <= x1) {
            return {};
        }
        return { x1, y + 1.0f, juce::jmax(3.0f, x2 - x1), getRowHeight() - 2.0f };
    }

    int hitTestNote(juce::Point<float> position) const {
        for (int i = static_cast<int>(notes.size()) - 1; i >= 0; i--) {
            if (noteBounds(i).expanded(0.0f, 1.0f).contains(position)) {
                return i;
            }
        }
        return -1;
    }

    void centreOnNotes() {
        if (notes.empty()) {
            lowestPitch = 48;
        } else {
            int lo = 127, hi = 0;
            for (const auto& note : notes) {
                lo = juce::jmin(lo, note.pitch);
                hi = juce::jmax(hi, note.pitch);
            }
            lowestPitch = juce::jlimit(0, juce::jmax(0, 127 - getNumRows() + 1), (lo + hi) / 2 - getNumRows() / 2);
        }
        fitView();
    }

    void scrollPitchBy(int rows) {
        lowestPitch = juce::jlimit(0, juce::jmax(0, 127 - getNumRows() + 1), lowestPitch + rows);
        repaint();
    }

    // ---- editing ----

    double snapBeat(double beat) const {
        return snap ? std::round(beat / grid) * grid : beat;
    }

    void preview() {
        auto copy = (braceData != nullptr ? braceData : data)->copy();
        copy->notes = notes;
        copy->sanitise();
        processorRef.setClipData(trackIndex, clipIndex, copy);
        repaint();
    }

    void moveSelection(juce::Point<float> delta) {
        const double beatDelta = delta.x / getGridArea().getWidth() * viewLength;
        int pitchDelta = -static_cast<int>(std::round(delta.y / getRowHeight()));
        if (fold) {
            pitchDelta = 0; // folded rows aren't contiguous pitches
        }
        notes = dragStartNotes;
        for (int index : selection) {
            auto& note = notes[static_cast<size_t>(index)];
            const auto& original = dragStartNotes[static_cast<size_t>(index)];
            // Keep the note's offset from the grid while snapping (preserves feel).
            const double offset = original.start - std::round(original.start / grid) * grid;
            note.start = juce::jmax(0.0, (snap ? snapBeat(original.start + beatDelta - offset) + offset
                                              : original.start + beatDelta));
            note.pitch = juce::jlimit(0, 127, original.pitch + pitchDelta);
        }
        changed = true;
        preview();
    }

    void resizeSelection(float deltaX) {
        const double beatDelta = deltaX / getGridArea().getWidth() * viewLength;
        notes = dragStartNotes;
        for (int index : selection) {
            auto& note = notes[static_cast<size_t>(index)];
            const auto& original = dragStartNotes[static_cast<size_t>(index)];
            double end = original.end() + beatDelta;
            end = snap ? snapBeat(end) : end;
            note.length = juce::jmax(snap ? grid : 1.0 / 64.0, end - note.start);
        }
        changed = true;
        preview();
    }

    void setVelocityAt(juce::Point<float> position) {
        const auto area = getVelocityArea();
        const int velocity = juce::jlimit(1, 127, static_cast<int>((area.getBottom() - position.y) / (area.getHeight() - 6.0f) * 127.0f));
        // Selected notes all follow; otherwise the note(s) nearest in time to the finger.
        std::vector<int> targets;
        if (!selection.empty()) {
            targets.assign(selection.begin(), selection.end());
        } else {
            float best = 12.0f;
            for (int i = 0; i < static_cast<int>(notes.size()); i++) {
                const float distance = std::abs(beatToX(notes[static_cast<size_t>(i)].start) - position.x);
                if (distance < best - 0.5f) {
                    best = distance;
                    targets = { i };
                } else if (std::abs(distance - best) <= 0.5f) {
                    targets.push_back(i);
                }
            }
        }
        if (targets.empty()) {
            return;
        }
        for (int index : targets) {
            notes[static_cast<size_t>(index)].velocity = velocity;
        }
        lastVelocity = velocity;
        changed = true;
        preview();
    }

    void resolveOverlaps(int editedIndex) {
        notes = ClipEdits::resolveOverlaps(notes, editedIndex);
    }

    int indexOf(const MidiNote& note) const {
        for (int i = 0; i < static_cast<int>(notes.size()); i++) {
            if (notes[static_cast<size_t>(i)] == note) {
                return i;
            }
        }
        return -1;
    }

    std::vector<MidiNote> selectedNotes(const std::vector<MidiNote>& from) const {
        std::vector<MidiNote> result;
        for (int index : selection) {
            if (index < static_cast<int>(from.size())) {
                result.push_back(from[static_cast<size_t>(index)]);
            }
        }
        return result;
    }

    void reselect(const std::vector<MidiNote>& wanted) {
        selection.clear();
        for (const auto& note : wanted) {
            const int index = indexOf(note);
            if (index >= 0) {
                selection.insert(index);
            }
        }
        notifySelection();
        repaint();
    }

    // ---- loop brace ----

    juce::Rectangle<float> braceBounds() const {
        const auto ruler = getRulerArea();
        const double from = data->looping ? data->loopStart : data->startMarker;
        const double to = data->looping ? data->loopEnd : data->endMarker;
        return juce::Rectangle<float>(beatToX(from), ruler.getY() + 12.0f, beatToX(to) - beatToX(from), 8.0f);
    }

    void beginBraceDrag(juce::Point<float> position) {
        const auto brace = braceBounds();
        if (std::abs(position.x - brace.getX()) < 10.0f) {
            dragMode = DragMode::BraceStart;
        } else if (std::abs(position.x - brace.getRight()) < 10.0f) {
            dragMode = DragMode::BraceEnd;
        } else if (brace.getHorizontalRange().contains(position.x)) {
            dragMode = DragMode::Brace;
        } else {
            return;
        }
        braceData = data->copy();
    }

    void dragBrace(float deltaX) {
        const double beatDelta = deltaX / getGridArea().getWidth() * viewLength;
        const auto& start = *dragStartData;
        auto edited = start.copy();
        const double step = snap ? grid : 1.0 / 64.0;
        auto snapTo = [step](double beat) { return std::round(beat / step) * step; };

        double& from = edited->looping ? edited->loopStart : edited->startMarker;
        double& to = edited->looping ? edited->loopEnd : edited->endMarker;
        const double origFrom = start.looping ? start.loopStart : start.startMarker;
        const double origTo = start.looping ? start.loopEnd : start.endMarker;

        if (dragMode == DragMode::BraceStart) {
            from = juce::jlimit(0.0, origTo - step, snapTo(origFrom + beatDelta));
        } else if (dragMode == DragMode::BraceEnd) {
            to = juce::jmax(origFrom + step, snapTo(origTo + beatDelta));
        } else {
            const double length = origTo - origFrom;
            from = juce::jmax(0.0, snapTo(origFrom + beatDelta));
            to = from + length;
        }
        if (edited->looping) {
            // Keep the start marker inside the clip; default it to the loop start when it was there.
            if (start.startMarker == start.loopStart) {
                edited->startMarker = edited->loopStart;
            }
            edited->endMarker = juce::jmax(edited->endMarker, edited->loopEnd);
        }
        edited->notes = notes;
        edited->sanitise();
        braceData = edited;
        changed = true;
        processorRef.setClipData(trackIndex, clipIndex, edited);
        data = edited;
        repaint();
    }
};
