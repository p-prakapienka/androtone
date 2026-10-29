namespace rb
{

juce::String pitchClassName (int pitchClass)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((pitchClass % 12) + 12) % 12];
}

juce::String noteName (int midiNote)
{
    const int octave = (int) std::floor (midiNote / 12.0) - 2;
    return pitchClassName (midiNote) + juce::String (octave);
}

const std::vector<Scale>& getScales()
{
    static const std::vector<Scale> scales {
        { "Major",            { 0, 2, 4, 5, 7, 9, 11 } },
        { "Minor",            { 0, 2, 3, 5, 7, 8, 10 } },
        { "Dorian",           { 0, 2, 3, 5, 7, 9, 10 } },
        { "Mixolydian",       { 0, 2, 4, 5, 7, 9, 10 } },
        { "Lydian",           { 0, 2, 4, 6, 7, 9, 11 } },
        { "Phrygian",         { 0, 1, 3, 5, 7, 8, 10 } },
        { "Locrian",          { 0, 1, 3, 5, 6, 8, 10 } },
        { "Harmonic Minor",   { 0, 2, 3, 5, 7, 8, 11 } },
        { "Melodic Minor",    { 0, 2, 3, 5, 7, 9, 11 } },
        { "Major Pentatonic", { 0, 2, 4, 7, 9 } },
        { "Minor Pentatonic", { 0, 3, 5, 7, 10 } },
        { "Blues",            { 0, 3, 5, 6, 7, 10 } },
        { "Whole Tone",       { 0, 2, 4, 6, 8, 10 } },
        { "Chromatic",        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
    };
    return scales;
}

static const Scale& scaleAt (int scaleIndex)
{
    const auto& scales = getScales();
    return scales[(size_t) juce::jlimit (0, (int) scales.size() - 1, scaleIndex)];
}

bool isInScale (int midiNote, int root, int scaleIndex)
{
    const int degree = (((midiNote - root) % 12) + 12) % 12;
    for (auto interval : scaleAt (scaleIndex).intervals)
        if (interval == degree)
            return true;
    return false;
}

int snapToScale (int midiNote, int root, int scaleIndex)
{
    for (int distance = 0; distance < 12; ++distance)
    {
        if (isInScale (midiNote - distance, root, scaleIndex)) return midiNote - distance;
        if (isInScale (midiNote + distance, root, scaleIndex)) return midiNote + distance;
    }
    return midiNote;
}

int transposeInScale (int midiNote, int steps, bool inKey, int root, int scaleIndex)
{
    if (! inKey)
        return juce::jlimit (0, 127, midiNote + steps);

    int note = snapToScale (midiNote, root, scaleIndex);
    const int direction = steps > 0 ? 1 : -1;

    for (int i = 0; i < std::abs (steps); ++i)
    {
        do { note += direction; }
        while (note > 0 && note < 127 && ! isInScale (note, root, scaleIndex));
    }

    return juce::jlimit (0, 127, note);
}

const std::vector<GridDivision>& getGridDivisions()
{
    static const std::vector<GridDivision> divisions {
        { "1/4", 1.0 }, { "1/8", 0.5 }, { "1/16", 0.25 }, { "1/32", 0.125 },
    };
    return divisions;
}

} // namespace rb
