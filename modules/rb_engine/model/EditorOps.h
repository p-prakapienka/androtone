#pragma once

namespace rb
{

// Pure helpers behind the touch MIDI editor (EDIT-2..EDIT-12): grid snapping, row layout,
// note hit-testing, drawing runs, velocity ramps and loop-brace bar toggling. No GUI types, so
// they are unit-tested headless (Tests/EditorTests.cpp).

//==============================================================================
// Grid
double snapToGrid (double beat, double grid);  // nearest grid line; grid <= 0 returns beat
double floorToGrid (double beat, double grid); // grid line at or before beat (tolerant of rounding)

// The editor's adaptive grid: the finest division of getGridDivisions() (or its triplet) whose
// cells are at least minCellPixels wide at this zoom; 1/4 (or 1/4T) when even that is narrower.
double editorGridForZoom (double pixelsPerBeat, bool triplets, double minCellPixels = 18.0);

//==============================================================================
// Rows (pitches, lowest first)

// Every row of the expanded editor: the 16 drum pads, or the full key range (only in-key
// pitches when inKey).
std::vector<int> editorRows (bool isDrum, bool inKey, int root, int scale);

// Rows of the inline preview: only pitches that have notes in [fromBeat, toBeat). Drums add the
// selected pad; when nothing is left, melodic clips fall back to C2..C4 (in key if inKey).
std::vector<int> previewRows (const std::vector<Note>& notes, const std::vector<Note>& pending,
                              double fromBeat, double toBeat, bool isDrum, int selectedPad,
                              bool inKey, int root, int scale);

// Index of pitch in rows, or -1.
int rowIndexOf (const std::vector<int>& rows, int pitch);

//==============================================================================
// Hit testing in beat/pitch space. edgeBeats is the width of the right-edge (resize) zone and
// also how far past a note's end a touch still counts as that edge.
struct NoteHit
{
    int index = -1;
    bool rightEdge = false;
};

NoteHit hitTestNote (const Clip&, double beat, int pitch, double edgeBeats);

//==============================================================================
// Drawing (EDIT-5, EDIT-6)

// Drum draw: one note per grid cell from the cell holding fromBeat to the cell holding toBeat
// (either order), each one cell long, inside the clip.
std::vector<Note> makeDrumRun (int pitch, double fromBeat, double toBeat, double grid, float velocity,
                               double clipLengthBeats);

// Melodic draw: one note covering the cells from fromBeat to toBeat (at least one cell).
Note makeDrawnNote (int pitch, double fromBeat, double toBeat, double grid, float velocity, double clipLengthBeats);

// Adds notes, skipping any that would start (within 1/64 beat) on the same pitch as an existing
// note. The selection holds the indices of the notes actually added.
ClipEdit addNotes (const Clip&, const std::vector<Note>&);

//==============================================================================
// Velocity tab (EDIT-10)

// count values from first to last, evenly spaced (two-finger ramp).
std::vector<float> velocityRamp (size_t count, float first, float last);

// Swipe drawing: sets values[fromIndex..toIndex] by linear interpolation between the two values.
void drawValues (std::vector<float>& values, int fromIndex, float fromValue, int toIndex, float toValue);

//==============================================================================
// Loop brace (EDIT-12)
struct LoopRange
{
    double start = 0.0;
    double length = 4.0;
};

// Tapping a bar on the brace. A bar outside the loop extends the loop to include it. A bar at
// either end of the loop is removed from it; tapping a bar in the middle shortens the loop to
// end after that bar. The loop never drops below one bar, and bars outside 1..16 are ignored.
LoopRange toggleLoopBar (const Clip&, int bar);

// Dragging a brace edge to `beat`: snaps to whole beats, keeps the loop at least one beat long
// and inside 16 bars. The returned range may run past the clip (setLoop then grows the clip).
LoopRange dragLoopEdge (const Clip&, bool endEdge, double beat);

// Notes starting inside the loop, in time order (indices into clip.notes).
Selection notesInLoop (const Clip&);

} // namespace rb
