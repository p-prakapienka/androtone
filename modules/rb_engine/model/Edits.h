#pragma once

namespace rb
{

// Pure editing operations on the set model. The UI calls these inside Document::edit so
// each one becomes an undo step. Clips are immutable, so clip operations return new clips.

//==============================================================================
// Tracks (TRK)
Track makeTrack (Set&, const TrackPreset&);          // assigns a fresh id and colour
bool addTrack (Set&, const TrackPreset&);            // false when the set already has 8 tracks
int duplicateTrack (Set&, int trackId);              // returns the new track id, or 0
void deleteTrack (Set&, int trackId);
void moveTrack (Set&, int trackId, int newIndex);

//==============================================================================
// Clip slots (CLIP) and scenes (SCN)
void setClip (Set&, int trackId, int slot, ClipPtr);
void moveClip (Set&, int fromTrackId, int fromSlot, int toTrackId, int toSlot); // swaps when occupied
int duplicateClip (Set&, int trackId, int slot);     // returns the new slot, or -1 if the track is full
void deleteClip (Set&, int trackId, int slot);

void moveScene (Set&, int fromScene, int toScene);
bool duplicateScene (Set&, int scene);               // inserts below; false if the last row is in use
void deleteScene (Set&, int scene);                  // rows below move up
bool sceneIsEmpty (const Set&, int scene);

//==============================================================================
// Clip contents (EDIT). A selection is a list of indices into clip.notes; operations that
// move notes return the clip with the selection updated to the moved notes' new indices.
using Selection = std::vector<int>;

ClipPtr makeEmptyClip (int bars);
ClipPtr makeClip (std::vector<Note> notes, double lengthBeats); // length rounded up to whole bars

struct ClipEdit
{
    ClipPtr clip;
    Selection selection;
};

ClipEdit addNote (const Clip&, const Note&);
ClipEdit deleteNotes (const Clip&, const Selection&);
ClipEdit moveNotes (const Clip&, const Selection&, double deltaBeats, int deltaPitch);
ClipEdit resizeNotes (const Clip&, const Selection&, double deltaBeats, double minLength);
ClipEdit setNoteLength (const Clip&, int index, double length);

// Transpose: drums move between pads; melodic notes move by scale steps when inKey (EDIT-10).
ClipEdit transposeNotes (const Clip&, const Selection&, int steps, bool isDrum, bool inKey, int root, int scale);
ClipEdit transposeOctave (const Clip&, const Selection&, int octaves);

// Quantize to `grid` beats with strength 0..1. grid <= 0 restores the pre-quantize timing.
ClipEdit quantizeNotes (const Clip&, const Selection&, double grid, float strength);

ClipEdit setVelocities (const Clip&, const Selection&, const std::vector<float>& velocities);
ClipEdit setPitchOffsets (const Clip&, const Selection&, const std::vector<float>& offsets);

Selection selectAll (const Clip&);
Selection selectInRect (const Clip&, double beat0, double beat1, int pitch0, int pitch1);

// Loop brace (EDIT-12).
ClipPtr setLoop (const Clip&, double loopStart, double loopLength); // grows the clip if needed
ClipPtr duplicateLoop (const Clip&);       // doubles the loop, copying its notes (max 16 bars)
ClipPtr cropToLoop (const Clip&);          // clip becomes the loop region
ClipPtr deleteLoopContent (const Clip&);   // removes notes inside the loop
ClipPtr setClipBars (const Clip&, int bars);

// Demo content, generated in code (LIB-1).
std::vector<Set> makeDemoSets();

// Overdub (CAP-5): merges notes into a clip, replacing exact duplicates.
ClipPtr mergeNotes (const Clip&, const std::vector<Note>&);

} // namespace rb
