#pragma once

namespace rb
{

// The expanded editor's surface (EDIT-2..EDIT-9, EDIT-12): loop brace and time ruler on top,
// row labels (pad names / note names) on the left, the note grid below. Handles every touch
// gesture: tap to add/delete, hold-and-drag to draw or box-select, edge drags to resize,
// selected-note moves and velocity drags, one-finger swipe scroll and two-finger pinch zoom.
class ClipEditorGrid : public juce::Component,
                       private juce::Timer
{
public:
    explicit ClipEditorGrid (ClipEditor&);

    // Re-reads rows and the clip (call after any model change).
    void refresh();
    void updatePlayhead();
    void zoomToRange (double startBeat, double lengthBeats);

    double getAdaptiveGrid() const;
    bool isGestureActive() const { return mode != Mode::none; }
    juce::Rectangle<int> getGridArea() const { return gridArea; } // local coordinates

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

private:
    enum class Mode
    {
        none,
        pending,         // finger down, waiting for a move or a long press
        holdEmpty,       // long-pressed empty space; the first move picks draw or box
        scroll,
        pinch,
        moveNotes,
        resizeSelection,
        resizeNote,
        velocity,
        extendSelection,
        draw,
        selectBox,
        braceEdge,
        rulerPending,
        rulerScroll,
        labels,
        finished         // gesture used up (e.g. a swipe that zoomed); ignore until release
    };

    enum class Region { grid, ruler, brace, labels, corner };

    void timerCallback() override; // long press

    // Geometry
    float beatToX (double beat) const;
    double xToBeat (float x) const;
    float rowTop (int rowIndex) const;       // y of the top of a row (rows are lowest pitch first)
    int rowAt (float y) const;               // row index, clamped
    float rowHeight() const { return rowH; }
    juce::Rectangle<float> noteBounds (const Note&) const;
    juce::Rectangle<float> velocityHandle (const Note&) const;
    double contentBeats() const;
    void clampView();
    void ensureViewInitialised();
    Region regionAt (juce::Point<float>) const;

    // Painting
    void paintRows (juce::Graphics&, const Track&, const Clip&);
    void paintNotes (juce::Graphics&, const Track&, const Clip&);
    void paintRuler (juce::Graphics&, const Track&, const Clip&);
    void paintLabels (juce::Graphics&, const Track&);
    void paintAutomation (juce::Graphics&, const Track&, const Clip&);
    float playheadX() const;

    // Gestures
    void startGesture (Mode);
    void finishGesture();
    void tap (juce::Point<float>);
    void longPressed();
    void dragTo (juce::Point<float>);
    void startPinch();
    void updatePinch();
    void addNoteAt (double beat, int rowIndex);
    int pitchAfterMove (int pitch, int rowDelta) const;

    ClipEditor& owner;

    std::vector<int> rows;
    juce::Rectangle<int> gridArea, rulerArea, braceArea, labelArea;
    float rowH = 30.0f;
    int labelWidth = 44;

    // Touches
    std::map<int, juce::Point<float>> touches;
    int primaryTouch = -1;
    Mode mode = Mode::none;
    Region downRegion = Region::grid;
    juce::Point<float> downPos, lastPos;
    double downBeat = 0.0;
    int downRow = 0;
    NoteHit downHit;
    bool downOnSelected = false;
    bool gestureOpen = false;

    // Drag bases: every drag step is computed from the clip as it was when the drag began.
    ClipPtr baseClip;
    Selection baseSelection;
    int anchorIndex = -1;
    double baseScrollBeat = 0.0, baseScrollY = 0.0, basePixelsPerBeat = 1.0;
    bool braceEnd = false;
    juce::String lastApplied;
    int lastAuditionPitch = -1;
    juce::Rectangle<float> selectionBox;

    // Pinch
    float pinchStartDistance = 1.0f;
    juce::Point<float> pinchStartMid;

    float lastPlayhead = -1.0f;
};

} // namespace rb
