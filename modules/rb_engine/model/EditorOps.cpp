namespace rb
{

static constexpr double gridEpsilon = 1.0e-6;

//==============================================================================
double snapToGrid (double beat, double grid)
{
    if (grid <= 0.0)
        return beat;
    return std::round (beat / grid) * grid;
}

double floorToGrid (double beat, double grid)
{
    if (grid <= 0.0)
        return beat;
    return std::floor (beat / grid + gridEpsilon) * grid;
}

double editorGridForZoom (double pixelsPerBeat, bool triplets, double minCellPixels)
{
    const auto& divisions = getGridDivisions(); // coarse to fine
    double chosen = divisions.front().beats;

    for (auto& d : divisions)
    {
        const double beats = triplets ? tripletOf (d.beats) : d.beats;
        if (beats * pixelsPerBeat >= minCellPixels - gridEpsilon)
            chosen = d.beats;
    }
    return triplets ? tripletOf (chosen) : chosen;
}

//==============================================================================
std::vector<int> editorRows (bool isDrum, bool inKey, int root, int scale)
{
    std::vector<int> rows;
    if (isDrum)
    {
        for (int pad = 0; pad < numDrumPads; ++pad)
            rows.push_back (drumBaseNote + pad);
        return rows;
    }

    for (int pitch = 0; pitch < 128; ++pitch)
        if (! inKey || isInScale (pitch, root, scale))
            rows.push_back (pitch);
    return rows;
}

std::vector<int> previewRows (const std::vector<Note>& notes, const std::vector<Note>& pending,
                              double fromBeat, double toBeat, bool isDrum, int selectedPad,
                              bool inKey, int root, int scale)
{
    std::vector<int> pitches;
    for (auto* list : { &notes, &pending })
        for (auto& n : *list)
            if (n.start >= fromBeat - gridEpsilon && n.start < toBeat - gridEpsilon)
                pitches.push_back (n.pitch);

    if (isDrum)
        pitches.push_back (drumBaseNote + juce::jlimit (0, numDrumPads - 1, selectedPad));
    else if (pitches.empty())
        for (int pitch = 48; pitch <= 72; ++pitch)
            if (! inKey || isInScale (pitch, root, scale))
                pitches.push_back (pitch);

    std::sort (pitches.begin(), pitches.end());
    pitches.erase (std::unique (pitches.begin(), pitches.end()), pitches.end());
    return pitches;
}

int rowIndexOf (const std::vector<int>& rows, int pitch)
{
    auto it = std::lower_bound (rows.begin(), rows.end(), pitch);
    return it != rows.end() && *it == pitch ? (int) (it - rows.begin()) : -1;
}

//==============================================================================
NoteHit hitTestNote (const Clip& clip, double beat, int pitch, double edgeBeats)
{
    NoteHit hit;

    // A note covering the point wins; notes are sorted by start and a later one is drawn on top.
    for (size_t i = 0; i < clip.notes.size(); ++i)
    {
        auto& n = clip.notes[i];
        if (n.pitch == pitch && beat >= n.start && beat < n.start + n.length)
        {
            hit.index = (int) i;
            // Short notes are mostly edge: keep at least their first half for moving.
            const double zone = juce::jmin (edgeBeats, n.length * 0.5);
            hit.rightEdge = beat >= n.start + n.length - zone;
        }
    }
    if (hit.index >= 0)
        return hit;

    // Just past a note's end still grabs its edge, so short notes stay resizable.
    for (size_t i = 0; i < clip.notes.size(); ++i)
    {
        auto& n = clip.notes[i];
        const double end = n.start + n.length;
        if (n.pitch == pitch && beat >= end && beat < end + edgeBeats)
        {
            hit.index = (int) i;
            hit.rightEdge = true;
        }
    }
    return hit;
}

//==============================================================================
std::vector<Note> makeDrumRun (int pitch, double fromBeat, double toBeat, double grid, float velocity,
                               double clipLengthBeats)
{
    std::vector<Note> notes;
    if (grid <= 0.0)
        return notes;

    if (toBeat < fromBeat)
        std::swap (fromBeat, toBeat);

    const double first = juce::jmax (0.0, floorToGrid (fromBeat, grid));
    const double last = floorToGrid (toBeat, grid);

    for (double start = first; start <= last + gridEpsilon && start < clipLengthBeats - gridEpsilon; start += grid)
    {
        Note n;
        n.start = start;
        n.length = grid;
        n.pitch = pitch;
        n.velocity = velocity;
        notes.push_back (n);
    }
    return notes;
}

Note makeDrawnNote (int pitch, double fromBeat, double toBeat, double grid, float velocity, double clipLengthBeats)
{
    if (toBeat < fromBeat)
        std::swap (fromBeat, toBeat);

    Note n;
    n.pitch = pitch;
    n.velocity = velocity;

    if (grid <= 0.0)
    {
        n.start = juce::jlimit (0.0, juce::jmax (0.0, clipLengthBeats - 1.0 / 128.0), fromBeat);
        n.length = juce::jmax (1.0 / 128.0, toBeat - fromBeat);
    }
    else
    {
        n.start = juce::jlimit (0.0, juce::jmax (0.0, clipLengthBeats - grid), floorToGrid (fromBeat, grid));
        const double end = floorToGrid (toBeat, grid) + grid;
        n.length = juce::jmax (grid, end - n.start);
    }

    n.length = juce::jmin (n.length, clipLengthBeats - n.start);
    return n;
}

ClipEdit addNotes (const Clip& clip, const std::vector<Note>& added)
{
    Clip copy = clip;
    std::vector<Note> fresh;

    for (auto& n : added)
    {
        auto clash = [&] (const Note& existing)
        {
            return existing.pitch == n.pitch && std::abs (existing.start - n.start) < 1.0 / 64.0;
        };
        if (std::none_of (copy.notes.begin(), copy.notes.end(), clash)
            && std::none_of (fresh.begin(), fresh.end(), clash))
            fresh.push_back (n);
    }

    // Tag the new notes so their sorted indices can be reported as the selection.
    const size_t firstNew = copy.notes.size();
    copy.notes.insert (copy.notes.end(), fresh.begin(), fresh.end());

    std::vector<std::pair<Note, bool>> tagged;
    for (size_t i = 0; i < copy.notes.size(); ++i)
        tagged.emplace_back (copy.notes[i], i >= firstNew);

    std::stable_sort (tagged.begin(), tagged.end(), [] (auto& a, auto& b)
    {
        if (a.first.start < b.first.start) return true;
        if (b.first.start < a.first.start) return false;
        return a.first.pitch < b.first.pitch;
    });

    ClipEdit result;
    copy.notes.clear();
    for (size_t i = 0; i < tagged.size(); ++i)
    {
        copy.notes.push_back (tagged[i].first);
        if (tagged[i].second)
            result.selection.push_back ((int) i);
    }
    result.clip = std::make_shared<Clip> (std::move (copy));
    return result;
}

//==============================================================================
std::vector<float> velocityRamp (size_t count, float first, float last)
{
    std::vector<float> values (count, first);
    for (size_t i = 0; i < count && count > 1; ++i)
        values[i] = first + (last - first) * (float) i / (float) (count - 1);
    return values;
}

void drawValues (std::vector<float>& values, int fromIndex, float fromValue, int toIndex, float toValue)
{
    if (values.empty())
        return;

    if (toIndex < fromIndex)
    {
        std::swap (fromIndex, toIndex);
        std::swap (fromValue, toValue);
    }

    for (int i = juce::jmax (0, fromIndex); i <= toIndex && i < (int) values.size(); ++i)
    {
        const float t = toIndex == fromIndex ? 1.0f : (float) (i - fromIndex) / (float) (toIndex - fromIndex);
        values[(size_t) i] = fromValue + (toValue - fromValue) * t;
    }
}

//==============================================================================
LoopRange toggleLoopBar (const Clip& clip, int bar)
{
    LoopRange range { clip.loopStart, clip.loopLength };
    if (bar < 0 || bar >= maxClipBars)
        return range;

    const double barStart = bar * (double) beatsPerBar;
    const double barEnd = barStart + beatsPerBar;
    const double loopEnd = clip.loopEnd();

    if (barStart >= loopEnd - gridEpsilon)
    {
        range.length = barEnd - range.start; // extend right
    }
    else if (barEnd <= clip.loopStart + gridEpsilon)
    {
        range.start = barStart;              // extend left
        range.length = loopEnd - barStart;
    }
    else if (clip.loopLength > beatsPerBar + gridEpsilon)
    {
        if (barEnd >= loopEnd - gridEpsilon)
            range.length = barStart - range.start;          // last bar: remove it
        else if (barStart <= clip.loopStart + gridEpsilon)
        {
            range.start = barEnd;                            // first bar: remove it
            range.length = loopEnd - barEnd;
        }
        else
            range.length = barEnd - range.start;            // middle: loop ends after this bar
    }

    // Removing a partial bar can leave a sliver; keep at least one bar.
    if (range.length < beatsPerBar - gridEpsilon)
    {
        range.start = juce::jmin (range.start, maxClipBars * (double) beatsPerBar - beatsPerBar);
        range.length = beatsPerBar;
    }
    return range;
}

LoopRange dragLoopEdge (const Clip& clip, bool endEdge, double beat)
{
    const double maxBeats = maxClipBars * (double) beatsPerBar;
    const double snapped = juce::jlimit (0.0, maxBeats, std::round (beat));
    LoopRange range { clip.loopStart, clip.loopLength };

    if (endEdge)
    {
        const double end = juce::jmax (clip.loopStart + 1.0, snapped);
        range.length = end - clip.loopStart;
    }
    else
    {
        const double start = juce::jmin (clip.loopEnd() - 1.0, snapped);
        range.start = juce::jmax (0.0, start);
        range.length = clip.loopEnd() - range.start;
    }
    return range;
}

Selection notesInLoop (const Clip& clip)
{
    Selection s;
    for (size_t i = 0; i < clip.notes.size(); ++i)
        if (clip.notes[i].start >= clip.loopStart - gridEpsilon && clip.notes[i].start < clip.loopEnd() - gridEpsilon)
            s.push_back ((int) i);
    return s;
}

} // namespace rb
