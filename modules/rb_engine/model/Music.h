#pragma once

namespace rb
{

// Limits from the spec (TRK-1, SCN-1, CLIP-6, PAD-1).
constexpr int maxTracks = 8;
constexpr int numScenes = 8;
constexpr int beatsPerBar = 4;
constexpr int maxClipBars = 16;
constexpr int numDrumPads = 16;

// MIDI note of the first drum pad (bottom-left). Pad n plays note drumBaseNote + n.
constexpr int drumBaseNote = 36;

// The Sampler plays a sample at its original pitch on this note ("C3" in the app's naming).
constexpr int samplerRootNote = 60;

constexpr double minTempo = 20.0;
constexpr double maxTempo = 999.0;

// Names use the convention where MIDI 60 is C3.
juce::String noteName (int midiNote);
juce::String pitchClassName (int pitchClass);

struct Scale
{
    const char* name;
    std::vector<int> intervals; // semitones from the root, ascending, first is 0
};

const std::vector<Scale>& getScales();

// True when midiNote belongs to the scale rooted at root (0 = C).
bool isInScale (int midiNote, int root, int scaleIndex);

// Moves a note by `steps` scale degrees (when inKey) or semitones (otherwise).
int transposeInScale (int midiNote, int steps, bool inKey, int root, int scaleIndex);

// Rounds a note to the nearest in-scale note, preferring the lower one on ties.
int snapToScale (int midiNote, int root, int scaleIndex);

// Beat divisions offered by grid/quantize/repeat menus, in beats.
struct GridDivision
{
    const char* label;
    double beats;
};

const std::vector<GridDivision>& getGridDivisions(); // 1/4 .. 1/32, no triplets

// A triplet division is two thirds of the straight one (1/4T = 2/3 beat).
inline double tripletOf (double beats) { return beats * 2.0 / 3.0; }

} // namespace rb
