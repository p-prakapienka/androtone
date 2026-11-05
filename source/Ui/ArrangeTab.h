#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"
#include "AndrotoneLookAndFeel.h"

// The arrangement timeline: one lane per track holding clips placed in song time.
//
// Ruler: tap to set the insert marker (where playback starts); drag the loop brace's edges or body.
// Clips: tap to select, drag to move (also between tracks), drag the right edge to resize (looping
// content fills the new length), double-tap to delete. Edits commit as one undo step per gesture.
class ArrangementView : public juce::Component, private juce::Timer {
public:
    explicit ArrangementView(AndrotoneAudioProcessor& p) : processorRef(p) {
        startTimerHz(30);
    }

    std::function<void()> onSelectionChanged;

    // Which envelope the lanes show and edit: -1 = none (clip editing), else an AutomatedParam.
    void setAutomationParam(int param) {
        automationParam = param;
        clearSelection();
        repaint();
    }

    bool hasSelection() const { return selectedTrack >= 0 && selectedIndex >= 0; }

    void deleteSelection() {
        if (!hasSelection()) {
            return;
        }
        const auto before = processorRef.getArrangement();
        auto after = before->copy();
        after->remove(selectedTrack, selectedIndex);
        after->render();
        processorRef.performArrangementEdit(before, after, "Delete arrangement clip");
        clearSelection();
    }

    void duplicateSelection() {
        if (!hasSelection()) {
            return;
        }
        const auto before = processorRef.getArrangement();
        const auto& lane = before->clips[static_cast<size_t>(selectedTrack)];
        if (selectedIndex >= static_cast<int>(lane.size())) {
            return;
        }
        ArrangementClip copy = lane[static_cast<size_t>(selectedIndex)];
        copy.start = copy.end();
        auto after = before->copy();
        after->place(selectedTrack, copy);
        after->render();
        processorRef.performArrangementEdit(before, after, "Duplicate arrangement clip");
        selectAt(selectedTrack, copy.start);
    }

    void zoomBy(double factor) {
        viewLength = juce::jlimit(4.0, 1024.0, viewLength * factor);
        repaint();
    }

    void clearSelection() {
        selectedTrack = selectedIndex = -1;
        if (onSelectionChanged) {
            onSelectionChanged();
        }
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const juce::Colour background { AndrotoneLookAndFeel::Palette::BACKGROUND };
        const juce::Colour surface { AndrotoneLookAndFeel::Palette::SURFACE };
        const juce::Colour outline { AndrotoneLookAndFeel::Palette::OUTLINE };
        const juce::Colour muted { AndrotoneLookAndFeel::Palette::TEXT_MUTED };
        const juce::Colour accent { AndrotoneLookAndFeel::Palette::ACCENT };
        const auto data = processorRef.getArrangement();

        g.fillAll(background);

        // Ruler: bar numbers, loop brace, insert marker.
        const auto ruler = getRulerArea();
        g.setColour(surface);
        g.fillRect(ruler);
        const double barStep = viewLength > 128.0 ? 16.0 : (viewLength > 64.0 ? 8.0 : 4.0);
        for (double beat = std::floor(viewStart / barStep) * barStep; beat < viewStart + viewLength; beat += barStep) {
            if (beat < viewStart) {
                continue;
            }
            const float x = beatToX(beat);
            g.setColour(outline);
            g.drawVerticalLine(static_cast<int>(x), ruler.getBottom(), static_cast<float>(getHeight()));
            g.setColour(muted);
            g.setFont(juce::FontOptions(11.0f));
            g.drawText(juce::String(static_cast<int>(beat / 4.0) + 1), juce::Rectangle<float>(x + 3.0f, ruler.getY(), 40.0f, 13.0f),
                       juce::Justification::centredLeft);
        }
        const auto brace = braceBounds();
        g.setColour(processorRef.isLoopEnabled() ? accent : muted.withAlpha(0.6f));
        g.fillRect(brace);
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.fillRect(brace.withWidth(3.0f));
        g.fillRect(brace.withX(brace.getRight() - 3.0f).withWidth(3.0f));

        // Lanes.
        for (int t = 0; t < numTracks(); t++) {
            const auto lane = laneBounds(t);
            const auto colour = AndrotoneAudioProcessor::getTrackColour(t);
            g.setColour(t % 2 == 0 ? surface.withAlpha(0.5f) : background);
            g.fillRect(lane.withX(static_cast<float>(HEADER_WIDTH)));
            auto header = lane.withWidth(static_cast<float>(HEADER_WIDTH) - 4.0f);
            g.setColour(colour.withAlpha(processorRef.followsArrangement(t) ? 0.9f : 0.35f));
            g.fillRoundedRectangle(header.reduced(2.0f), 3.0f);
            g.setColour(background);
            g.drawText(juce::String(t + 1), header, juce::Justification::centred);

            if (data == nullptr) {
                continue;
            }
            const auto& clips = data->clips[static_cast<size_t>(t)];
            for (int i = 0; i < static_cast<int>(clips.size()); i++) {
                const auto box = clipBounds(t, clips[static_cast<size_t>(i)]);
                if (box.getRight() < HEADER_WIDTH || box.getX() > getWidth()) {
                    continue;
                }
                const bool selected = t == selectedTrack && i == selectedIndex;
                g.setColour(colour.withAlpha(0.35f));
                g.fillRoundedRectangle(box, 3.0f);
                g.setColour(selected ? juce::Colours::white : colour);
                g.drawRoundedRectangle(box, 3.0f, selected ? 2.0f : 1.0f);
                drawNotes(g, *data, t, clips[static_cast<size_t>(i)], box.reduced(2.0f, 12.0f), colour);
                const auto& content = clips[static_cast<size_t>(i)].content;
                if (content != nullptr && content->name.isNotEmpty()) {
                    g.setColour(juce::Colour(AndrotoneLookAndFeel::Palette::TEXT));
                    g.setFont(juce::FontOptions(10.0f));
                    g.drawText(content->name, box.reduced(4.0f, 1.0f).removeFromTop(11.0f), juce::Justification::centredLeft);
                }
            }
        }

        // Automation envelopes over the lanes.
        if (automationParam >= 0 && data != nullptr) {
            for (int t = 0; t < numTracks(); t++) {
                const auto lane = laneBounds(t).reduced(0.0f, 4.0f).withLeft(static_cast<float>(HEADER_WIDTH));
                const auto& points = data->automation[static_cast<size_t>(t)][static_cast<size_t>(automationParam)];
                g.setColour(juce::Colours::black.withAlpha(0.35f));
                g.fillRect(lane);
                const auto colour = juce::Colour(0xFFE57373);
                if (points.empty()) {
                    g.setColour(muted);
                    g.setFont(juce::FontOptions(11.0f));
                    g.drawText("Tap to add a breakpoint", lane.reduced(8.0f, 2.0f), juce::Justification::topLeft);
                    continue;
                }
                juce::Path path;
                path.startNewSubPath(lane.getX(), valueToY(points.front().value, lane));
                for (const auto& point : points) {
                    path.lineTo(beatToX(point.beat), valueToY(point.value, lane));
                }
                path.lineTo(lane.getRight(), valueToY(points.back().value, lane));
                g.setColour(colour);
                g.strokePath(path, juce::PathStrokeType(2.0f));
                for (const auto& point : points) {
                    g.fillEllipse(beatToX(point.beat) - 4.0f, valueToY(point.value, lane) - 4.0f, 8.0f, 8.0f);
                }
            }
        }

        // Insert marker and playhead.
        g.setColour(juce::Colour(0xFFFFB74D));
        const float insertX = beatToX(processorRef.getStartPosition());
        g.drawVerticalLine(static_cast<int>(insertX), ruler.getY(), static_cast<float>(getHeight()));
        if (processorRef.isPlaying()) {
            g.setColour(juce::Colours::white.withAlpha(0.85f));
            g.drawVerticalLine(static_cast<int>(beatToX(juce::jmax(0.0, processorRef.getSongPosition()))),
                               ruler.getY(), static_cast<float>(getHeight()));
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        dragStart = processorRef.getArrangement();
        downPosition = e.position;
        mode = Mode::None;
        moved = false;

        if (getRulerArea().contains(e.position)) {
            const auto brace = braceBounds();
            if (e.position.y >= brace.getY() - 2.0f) {
                if (std::abs(e.position.x - brace.getX()) < 10.0f) {
                    mode = Mode::LoopStart;
                } else if (std::abs(e.position.x - brace.getRight()) < 10.0f) {
                    mode = Mode::LoopEnd;
                } else if (brace.getHorizontalRange().contains(e.position.x)) {
                    mode = Mode::LoopMove;
                }
                loopStartAtDown = processorRef.getLoopStart();
                loopEndAtDown = processorRef.getLoopEnd();
                if (mode != Mode::None) {
                    return;
                }
            }
            // Tap in the ruler: insert marker (snapped to the beat).
            processorRef.setStartPosition(juce::jmax(0.0, std::round(xToBeat(e.position.x))));
            repaint();
            return;
        }

        const int t = laneAt(e.position.y);
        if (t < 0 || e.position.x < HEADER_WIDTH) {
            return;
        }
        if (automationParam >= 0) {
            beginAutomationDrag(t, e.position);
            return;
        }
        const int index = clipAt(t, e.position);
        if (index < 0) {
            clearSelection();
            processorRef.setStartPosition(juce::jmax(0.0, std::round(xToBeat(e.position.x))));
            return;
        }
        selectedTrack = t;
        selectedIndex = index;
        const auto& clip = dragStart->clips[static_cast<size_t>(t)][static_cast<size_t>(index)];
        draggedClip = clip;
        mode = e.position.x > clipBounds(t, clip).getRight() - 8.0f ? Mode::Resize : Mode::Move;
        if (onSelectionChanged) {
            onSelectionChanged();
        }
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        const double beats = (e.position.x - downPosition.x) / pixelsPerBeat();
        const double step = viewLength > 64.0 ? 4.0 : 1.0;
        auto snapTo = [step](double beat) { return std::round(beat / step) * step; };

        switch (mode) {
            case Mode::LoopStart:
                processorRef.setLoop(processorRef.isLoopEnabled(), juce::jlimit(0.0, loopEndAtDown - step, snapTo(loopStartAtDown + beats)), loopEndAtDown);
                break;
            case Mode::LoopEnd:
                processorRef.setLoop(processorRef.isLoopEnabled(), loopStartAtDown, juce::jmax(loopStartAtDown + step, snapTo(loopEndAtDown + beats)));
                break;
            case Mode::LoopMove: {
                const double start = juce::jmax(0.0, snapTo(loopStartAtDown + beats));
                processorRef.setLoop(processorRef.isLoopEnabled(), start, start + (loopEndAtDown - loopStartAtDown));
                break;
            }
            case Mode::Automation:
                dragAutomation(e.position);
                break;
            case Mode::Move:
            case Mode::Resize: {
                ArrangementClip edited = draggedClip;
                int targetTrack = selectedTrack;
                if (mode == Mode::Move) {
                    edited.start = juce::jmax(0.0, snapTo(draggedClip.start + beats));
                    const int lane = laneAt(e.position.y);
                    targetTrack = lane >= 0 ? lane : selectedTrack;
                } else {
                    edited.length = juce::jmax(step, snapTo(draggedClip.end() + beats) - draggedClip.start);
                }
                auto preview = dragStart->copy();
                preview->remove(selectedTrack, selectedIndex);
                preview->place(targetTrack, edited);
                preview->render();
                processorRef.setArrangementPreview(preview);
                previewTrack = targetTrack;
                previewStart = edited.start;
                moved = true;
                break;
            }
            case Mode::None:
                break;
        }
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override {
        if (mode == Mode::Automation && moved) {
            const auto after = processorRef.getArrangement();
            processorRef.setArrangementPreview(dragStart);
            processorRef.performArrangementEdit(dragStart, after, "Edit automation");
        }
        if ((mode == Mode::Move || mode == Mode::Resize) && moved) {
            const auto after = processorRef.getArrangement();
            processorRef.setArrangementPreview(dragStart); // restore, then commit as one undo step
            processorRef.performArrangementEdit(dragStart, after, mode == Mode::Move ? "Move arrangement clip" : "Resize arrangement clip");
            selectAt(previewTrack, previewStart);
        }
        mode = Mode::None;
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override {
        const int t = laneAt(e.position.y);
        if (automationParam >= 0) {
            if (t >= 0) {
                const auto before = processorRef.getArrangement();
                const int index = pointAt(*before, t, e.position);
                if (index >= 0) {
                    auto after = before->copy();
                    auto& points = after->automation[static_cast<size_t>(t)][static_cast<size_t>(automationParam)];
                    points.erase(points.begin() + index);
                    processorRef.performArrangementEdit(before, after, "Delete breakpoint");
                }
            }
            return;
        }
        if (t >= 0 && clipAt(t, e.position) >= 0) {
            selectedTrack = t;
            selectedIndex = clipAt(t, e.position);
            deleteSelection();
        }
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override {
        if (e.mods.isCommandDown() || e.mods.isCtrlDown()) {
            zoomBy(wheel.deltaY > 0 ? 0.8 : 1.25);
        } else {
            viewStart = juce::jmax(0.0, viewStart - (wheel.deltaX + wheel.deltaY) * viewLength * 0.3);
            repaint();
        }
    }

private:
    enum class Mode { None, Move, Resize, LoopStart, LoopEnd, LoopMove, Automation };
    static constexpr int HEADER_WIDTH = 44;
    static constexpr int RULER_HEIGHT = 28;

    AndrotoneAudioProcessor& processorRef;
    double viewStart = 0.0;
    double viewLength = 64.0; // 16 bars
    int selectedTrack = -1;
    int selectedIndex = -1;

    Mode mode = Mode::None;
    ArrangementData::Ptr dragStart;
    ArrangementClip draggedClip;
    juce::Point<float> downPosition;
    double loopStartAtDown = 0.0, loopEndAtDown = 16.0;
    int previewTrack = -1;
    int automationParam = -1;
    int automationTrack = -1;
    int automationIndex = -1;

    float paramMin() const { return automationParam == AUTOMATE_PAN ? -1.0f : 0.0f; }
    float paramMax() const { return automationParam == AUTOMATE_PAN ? 1.0f : 1.25f; }

    float valueToY(float value, juce::Rectangle<float> lane) const {
        const float norm = (value - paramMin()) / (paramMax() - paramMin());
        return lane.getBottom() - norm * lane.getHeight();
    }

    float yToValue(float y, juce::Rectangle<float> lane) const {
        const float norm = juce::jlimit(0.0f, 1.0f, (lane.getBottom() - y) / lane.getHeight());
        return paramMin() + norm * (paramMax() - paramMin());
    }

    juce::Rectangle<float> automationLane(int t) const {
        return laneBounds(t).reduced(0.0f, 4.0f).withLeft(static_cast<float>(HEADER_WIDTH));
    }

    int pointAt(const ArrangementData& data, int t, juce::Point<float> position) const {
        const auto lane = automationLane(t);
        const auto& points = data.automation[static_cast<size_t>(t)][static_cast<size_t>(automationParam)];
        for (int i = 0; i < static_cast<int>(points.size()); i++) {
            const auto& point = points[static_cast<size_t>(i)];
            if (position.getDistanceFrom({ beatToX(point.beat), valueToY(point.value, lane) }) < 10.0f) {
                return i;
            }
        }
        return -1;
    }

    // Tap: grab the breakpoint under the finger, or add one there (snapped to the beat).
    void beginAutomationDrag(int t, juce::Point<float> position) {
        mode = Mode::Automation;
        automationTrack = t;
        automationIndex = pointAt(*dragStart, t, position);
        if (automationIndex < 0) {
            auto added = dragStart->copy();
            auto& points = added->automation[static_cast<size_t>(t)][static_cast<size_t>(automationParam)];
            const Breakpoint point { juce::jmax(0.0, std::round(xToBeat(position.x) * 4.0) / 4.0), yToValue(position.y, automationLane(t)) };
            points.push_back(point);
            added->sortAutomation(t, automationParam);
            for (int i = 0; i < static_cast<int>(points.size()); i++) {
                if (points[static_cast<size_t>(i)].beat == point.beat && points[static_cast<size_t>(i)].value == point.value) {
                    automationIndex = i;
                }
            }
            processorRef.setArrangementPreview(added);
            moved = true;
        }
        repaint();
    }

    void dragAutomation(juce::Point<float> position) {
        const auto current = processorRef.getArrangement();
        if (automationIndex < 0 || current == nullptr) {
            return;
        }
        auto edited = current->copy();
        auto& points = edited->automation[static_cast<size_t>(automationTrack)][static_cast<size_t>(automationParam)];
        if (automationIndex >= static_cast<int>(points.size())) {
            return;
        }
        // Keep the point between its neighbours so the envelope stays ordered.
        const double lo = automationIndex > 0 ? points[static_cast<size_t>(automationIndex - 1)].beat : 0.0;
        const double hi = automationIndex + 1 < static_cast<int>(points.size()) ? points[static_cast<size_t>(automationIndex + 1)].beat : 1.0e6;
        auto& point = points[static_cast<size_t>(automationIndex)];
        point.beat = juce::jlimit(lo, hi, std::round(xToBeat(position.x) * 4.0) / 4.0);
        point.value = yToValue(position.y, automationLane(automationTrack));
        processorRef.setArrangementPreview(edited);
        moved = true;
    }
    double previewStart = 0.0;
    bool moved = false;

    void timerCallback() override {
        repaint();
    }

    int numTracks() const { return processorRef.getNumActiveTracks(); }

    juce::Rectangle<float> getRulerArea() const {
        return { static_cast<float>(HEADER_WIDTH), 0.0f, static_cast<float>(getWidth() - HEADER_WIDTH), RULER_HEIGHT };
    }

    float laneHeight() const {
        return juce::jlimit(40.0f, 110.0f, (getHeight() - RULER_HEIGHT) / static_cast<float>(juce::jmax(1, numTracks())));
    }

    juce::Rectangle<float> laneBounds(int t) const {
        return { 0.0f, RULER_HEIGHT + t * laneHeight(), static_cast<float>(getWidth()), laneHeight() };
    }

    int laneAt(float y) const {
        const int t = static_cast<int>((y - RULER_HEIGHT) / laneHeight());
        return y >= RULER_HEIGHT && t < numTracks() ? t : -1;
    }

    double pixelsPerBeat() const {
        return (getWidth() - HEADER_WIDTH) / viewLength;
    }

    float beatToX(double beat) const {
        return HEADER_WIDTH + static_cast<float>((beat - viewStart) * pixelsPerBeat());
    }

    double xToBeat(float x) const {
        return viewStart + (x - HEADER_WIDTH) / pixelsPerBeat();
    }

    juce::Rectangle<float> clipBounds(int t, const ArrangementClip& clip) const {
        const auto lane = laneBounds(t).reduced(0.0f, 3.0f);
        return { beatToX(clip.start), lane.getY(), juce::jmax(4.0f, beatToX(clip.end()) - beatToX(clip.start)), lane.getHeight() };
    }

    int clipAt(int t, juce::Point<float> position) const {
        const auto data = processorRef.getArrangement();
        if (data == nullptr) {
            return -1;
        }
        const auto& clips = data->clips[static_cast<size_t>(t)];
        for (int i = 0; i < static_cast<int>(clips.size()); i++) {
            if (clipBounds(t, clips[static_cast<size_t>(i)]).contains(position)) {
                return i;
            }
        }
        return -1;
    }

    void selectAt(int t, double start) {
        const auto data = processorRef.getArrangement();
        selectedTrack = selectedIndex = -1;
        if (data != nullptr && t >= 0) {
            const auto& clips = data->clips[static_cast<size_t>(t)];
            for (int i = 0; i < static_cast<int>(clips.size()); i++) {
                if (std::abs(clips[static_cast<size_t>(i)].start - start) < 1e-6) {
                    selectedTrack = t;
                    selectedIndex = i;
                }
            }
        }
        if (onSelectionChanged) {
            onSelectionChanged();
        }
        repaint();
    }

    juce::Rectangle<float> braceBounds() const {
        const auto ruler = getRulerArea();
        const float x1 = beatToX(processorRef.getLoopStart());
        const float x2 = beatToX(processorRef.getLoopEnd());
        return { x1, ruler.getBottom() - 9.0f, x2 - x1, 7.0f };
    }

    void drawNotes(juce::Graphics& g, const ArrangementData& data, int t, const ArrangementClip& clip,
                   juce::Rectangle<float> area, juce::Colour colour) const {
        int lo = 127, hi = 0;
        const auto& notes = data.rendered[static_cast<size_t>(t)];
        for (const auto& note : notes) {
            if (note.start >= clip.start && note.start < clip.end()) {
                lo = juce::jmin(lo, note.pitch);
                hi = juce::jmax(hi, note.pitch);
            }
        }
        if (lo > hi) {
            return;
        }
        const float rowHeight = area.getHeight() / static_cast<float>(hi - lo + 1);
        g.setColour(colour);
        for (const auto& note : notes) {
            if (note.start < clip.start || note.start >= clip.end()) {
                continue;
            }
            const float x = beatToX(note.start);
            const float w = juce::jmax(1.5f, beatToX(note.end()) - x);
            const float y = area.getBottom() - (note.pitch - lo + 1) * rowHeight;
            g.fillRect(juce::Rectangle<float>(x, y, w, juce::jmax(1.5f, rowHeight - 1.0f)).getIntersection(area));
        }
    }
};

class ArrangeTab : public juce::Component, private juce::Timer {
public:
    explicit ArrangeTab(AndrotoneAudioProcessor& p) : processorRef(p), view(p) {
        addAndMakeVisible(view);
        view.onSelectionChanged = [this]() { updateButtons(); };

        backButton.setButtonText("Back to Arrangement");
        backButton.setTooltip("Tracks playing session clips return to their arrangement");
        backButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFFFB74D));
        backButton.onClick = [this]() { processorRef.backToArrangement(); };
        addAndMakeVisible(backButton);

        recordButton.setButtonText(juce::String::fromUTF8("\xe2\x97\x8f Arrangement")); // ● Arrangement
        recordButton.setTooltip("Record session clip launches into the arrangement");
        recordButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFE57373));
        recordButton.onClick = [this]() { processorRef.setArrangementRecord(!processorRef.isArrangementRecording()); };
        addAndMakeVisible(recordButton);

        loopButton.setButtonText("Loop");
        loopButton.setTooltip("Loop the song between the brace's edges");
        loopButton.onClick = [this]() {
            processorRef.setLoop(!processorRef.isLoopEnabled(), processorRef.getLoopStart(), processorRef.getLoopEnd());
        };
        addAndMakeVisible(loopButton);

        automationChooser.addItemList({ "Clips", "Volume", "Pan" }, 1);
        automationChooser.setSelectedItemIndex(0, juce::dontSendNotification);
        automationChooser.setTooltip("Edit clips, or the volume / pan automation of each track");
        automationChooser.onChange = [this]() { view.setAutomationParam(automationChooser.getSelectedItemIndex() - 1); };
        addAndMakeVisible(automationChooser);

        reenableButton.setButtonText("Re-enable Automation");
        reenableButton.setTooltip("Automated controls you moved by hand follow their envelopes again");
        reenableButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFE57373));
        reenableButton.onClick = [this]() { processorRef.reenableAutomation(); };
        addAndMakeVisible(reenableButton);

        duplicateButton.setButtonText("Duplicate");
        duplicateButton.onClick = [this]() { view.duplicateSelection(); };
        addAndMakeVisible(duplicateButton);
        deleteButton.setButtonText("Delete");
        deleteButton.onClick = [this]() { view.deleteSelection(); };
        addAndMakeVisible(deleteButton);
        zoomOut.setButtonText(juce::String::fromUTF8("\xe2\x88\x92"));
        zoomOut.onClick = [this]() { view.zoomBy(1.5); };
        addAndMakeVisible(zoomOut);
        zoomIn.setButtonText("+");
        zoomIn.onClick = [this]() { view.zoomBy(1.0 / 1.5); };
        addAndMakeVisible(zoomIn);

        updateButtons();
        startTimerHz(8);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(6);
        auto bar = bounds.removeFromTop(32);
        bounds.removeFromTop(6);
        auto place = [&bar](juce::Component& c, int width) { c.setBounds(bar.removeFromLeft(width).reduced(2, 0)); };
        place(backButton, 160);
        place(recordButton, 130);
        place(loopButton, 60);
        bar.removeFromLeft(10);
        place(duplicateButton, 84);
        place(deleteButton, 70);
        bar.removeFromLeft(10);
        place(zoomOut, 32);
        place(zoomIn, 32);
        bar.removeFromLeft(10);
        place(automationChooser, 86);
        place(reenableButton, 150);
        view.setBounds(bounds);
    }

private:
    AndrotoneAudioProcessor& processorRef;
    ArrangementView view;
    juce::TextButton backButton, recordButton, loopButton, duplicateButton, deleteButton, zoomOut, zoomIn, reenableButton;
    juce::ComboBox automationChooser;

    void timerCallback() override {
        updateButtons();
    }

    void updateButtons() {
        backButton.setToggleState(processorRef.isArrangementOverridden(), juce::dontSendNotification);
        recordButton.setToggleState(processorRef.isArrangementRecording(), juce::dontSendNotification);
        loopButton.setToggleState(processorRef.isLoopEnabled(), juce::dontSendNotification);
        reenableButton.setToggleState(processorRef.isAutomationOverridden(), juce::dontSendNotification);
        reenableButton.setEnabled(processorRef.isAutomationOverridden());
        duplicateButton.setEnabled(view.hasSelection());
        deleteButton.setEnabled(view.hasSelection());
    }
};
