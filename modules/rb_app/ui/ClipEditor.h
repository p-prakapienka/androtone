#pragma once

namespace rb
{

class ClipEditorGrid;
class NoteEditMenu;

// Clip preview and MIDI editor of the instrument view (EDIT-1..EDIT-12, CAP-2).
//
// Collapsed it is a compact preview of the clip's loop: notes (saturation = velocity), pending
// notes as outlines, automation lines (pending dotted, committed solid) and the playhead. Tap
// expands it, long-press offers loop operations; an empty slot offers fixed-length clip creation.
//
// Expanded it hosts the editing grid (ClipEditorGrid: ruler, loop brace, rows, gestures), a
// toolbar (undo/redo, note-edit menu, duplicate loop, triplets) and the note-edit menu
// (NoteEditMenu). The expand/collapse button can be dragged to any corner.
//
// Every edit goes through Document::edit; drags are wrapped in a gesture so they undo in one
// step. The selection is kept valid across edits from the ClipEdit results, and remapped by
// note value when the clip changes elsewhere (undo, overdub).
class ClipEditor : public juce::Component,
                   private Studio::Listener,
                   private juce::Timer
{
public:
    ClipEditor (Studio&, Navigator&, int trackId);
    ~ClipEditor() override;

    void setSlot (int slot);
    int getSlot() const { return slot; }

    bool isExpanded() const { return expanded; }
    void setExpanded (bool);
    std::function<void (bool expanded)> onExpandedChanged;

    // Opens the quantization tab of the note-edit menu (INS-5 long press).
    void showQuantizeSettings();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    //==============================================================================
    // Shared with ClipEditorGrid and NoteEditMenu.

    // Per-track view state, remembered while the instrument view is closed (EDIT-11).
    struct ViewState
    {
        bool expanded = false;
        double pixelsPerBeat = 0.0; // 0 = zoom to the loop on the next layout
        double scrollBeat = 0.0;
        double scrollY = -1.0;      // -1 = centre on the notes on the next layout
        int gridChoice = -1;        // -1 = follows zoom, 0 = off, n = getGridDivisions()[n - 1]
        bool triplets = false;
        int menuTab = 0;
        int corner = 1;             // expand button: 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right
        double lastDrawnLength = 0.0; // melodic tap length (EDIT-5); 0 = one cell
        int lastSlot = -1;
    };
    ViewState& state();

    Studio& getStudio() { return studio; }
    int getTrackId() const { return trackId; }
    const Track* track() const;
    const Clip* clip() const;
    ClipPtr clipPtr() const;
    bool isDrumClip() const;

    const Selection& getSelection() const { return selection; }
    void setSelection (Selection);
    // The selection, or every note in the loop when nothing is selected.
    Selection targetNotes() const;

    // Applies an edited clip to the slot as one undo step (or as part of an open gesture).
    void commit (const ClipEdit&, bool keepSelection = true);
    void commitClip (ClipPtr, bool keepSelection);
    void beginGesture();
    void endGesture();

    double getCellGrid() const; // column width: the chosen grid, or the zoom's adaptive grid
    double getSnapGrid() const; // snapping for moves and resizes; 0 when the grid is Off
    double getNudgeStep() const;
    void setGridChoice (int choice);
    void setTriplets (bool);

    void audition (int pitch, float velocity);

    bool isMenuOpen() const { return menuOpen; }
    void setMenuOpen (bool open, int tab = -1);

    void zoomToLoop();
    void zoomToClip();
    void showLoopMenu (juce::Component& target, bool fromPreview);
    void duplicateLoopAction();

    void deleteClipOrLoopContent();

private:
    class CornerButton;

    void studioChanged() override;
    void captureChanged() override;
    void timerCallback() override;

    void updateToolbar();
    void layoutCornerButton();
    juce::Rectangle<int> cornerArea() const;
    void showNewClipMenu();
    void paintPreview (juce::Graphics&, const Track&, const Clip&);
    juce::Rectangle<float> previewArea() const;
    float previewPlayheadX (const Clip&) const;

    Studio& studio;
    Navigator& nav;
    int trackId;
    int slot = -1;
    bool expanded = false;
    bool restored = false;
    bool menuOpen = false;
    bool menuAutoOpened = false;

    Selection selection;
    std::vector<Note> selectedNotes; // values of the selection, to remap after outside changes
    ClipPtr ownClip;                 // the clip our last commit produced

    std::unique_ptr<ClipEditorGrid> grid;
    std::unique_ptr<NoteEditMenu> menu;
    std::unique_ptr<CornerButton> cornerButton;

    IconButton undoButton { Icon::undo, "Undo" }, redoButton { Icon::redo, "Redo" },
               menuButton { Icon::edit, "Edit" }, duplicateLoopButton { Icon::duplicate, "Dup Loop" },
               tripletButton { Icon::triplet, "Triplets" };

    LongPress longPress;
    float lastPlayheadX = -1.0f;
    std::map<int, int> auditionTokens;
};

} // namespace rb
