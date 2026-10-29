namespace rb
{

int melodicBaseNote (const Set& set, const Track& track)
{
    const int root = set.inKey ? set.root : 0;
    return juce::jlimit (0, 108, 48 + root + juce::jlimit (-3, 3, track.perf.octave) * 12);
}

//==============================================================================
PadSurface::PadSurface (Studio& s, int id) : studio (s), trackId (id)
{
    setRepaintsOnMouseActivity (false);
    startTimerHz (30);
}

PadSurface::~PadSurface()
{
    for (auto& [source, note] : heldBySource)
        studio.noteOff (trackId, note);
}

void PadSurface::setTrack (int id)
{
    for (auto& [source, note] : heldBySource)
        studio.noteOff (trackId, note);
    heldBySource.clear();
    trackId = id;
    repaint();
}

const Track* PadSurface::track() const
{
    return studio.getSet().findTrack (trackId);
}

bool PadSurface::isLit (int note) const
{
    for (auto& [source, held] : heldBySource)
        if (held == note)
            return true;
    return studio.isNoteSounding (trackId, note);
}

float PadSurface::velocityAt (juce::Point<float> p, int) const
{
    // Without pressure data, the touch position within the pad sets the velocity: centre
    // hits are strong, edges a little softer.
    juce::ignoreUnused (p);
    return 0.85f;
}

void PadSurface::mouseDown (const juce::MouseEvent& e)
{
    const int note = noteAt (e.position);
    if (note < 0)
        return;

    float velocity = velocityAt (e.position, note);
    if (e.isPressureValid())
        velocity = juce::jlimit (0.2f, 1.0f, 0.3f + e.pressure);

    heldBySource[e.source.getIndex()] = note;
    studio.noteOn (trackId, note, velocity);
    if (onNoteTouched != nullptr)
        onNoteTouched (note);
    repaint();
}

void PadSurface::mouseDrag (const juce::MouseEvent& e)
{
    auto it = heldBySource.find (e.source.getIndex());
    const int note = noteAt (e.position);
    if (it == heldBySource.end() || note == it->second)
        return;

    studio.noteOff (trackId, it->second);
    if (note >= 0)
    {
        it->second = note;
        studio.noteOn (trackId, note, velocityAt (e.position, note));
    }
    else
    {
        heldBySource.erase (it);
    }
    repaint();
}

void PadSurface::mouseUp (const juce::MouseEvent& e)
{
    release (e.source.getIndex());
}

void PadSurface::release (int source)
{
    if (auto it = heldBySource.find (source); it != heldBySource.end())
    {
        studio.noteOff (trackId, it->second);
        heldBySource.erase (it);
        repaint();
    }
}

//==============================================================================
juce::Rectangle<float> DrumPadGrid::padBounds (int pad) const
{
    const int row = pad / 4, column = pad % 4;
    const float w = getWidth() / 4.0f, h = getHeight() / 4.0f;
    return juce::Rectangle<float> (column * w, (3 - row) * h, w, h).reduced (3.0f);
}

int DrumPadGrid::noteAt (juce::Point<float> p) const
{
    if (! getLocalBounds().toFloat().contains (p))
        return -1;
    const int column = juce::jlimit (0, 3, (int) (p.x / (getWidth() / 4.0f)));
    const int row = 3 - juce::jlimit (0, 3, (int) (p.y / (getHeight() / 4.0f)));
    return drumBaseNote + row * 4 + column;
}

void DrumPadGrid::paint (juce::Graphics& g)
{
    const auto& t = Theme::get();
    auto* tr = track();
    if (tr == nullptr)
        return;

    const auto colour = Theme::trackColour (tr->colour);
    const bool showNames = studio.getSettings().getShowSampleNames();

    for (int pad = 0; pad < numDrumPads; ++pad)
    {
        auto bounds = padBounds (pad);
        const bool hasSample = tr->pads[(size_t) pad].sampleRef.isNotEmpty();
        const bool lit = isLit (drumBaseNote + pad);
        const bool selected = tr->perf.selectedPad == pad;

        auto fill = hasSample ? colour.withMultipliedBrightness (0.45f).withMultipliedSaturation (0.8f) : t.surface;
        if (lit) fill = colour.brighter (0.2f);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 8.0f);

        if (selected)
        {
            g.setColour (t.text);
            g.drawRoundedRectangle (bounds.reduced (1.0f), 8.0f, 2.0f);
        }

        if (showNames && hasSample)
        {
            g.setColour (lit ? juce::Colours::black : t.text.withAlpha (0.85f));
            g.setFont (Theme::font (12.0f));
            g.drawFittedText (tr->pads[(size_t) pad].name, bounds.reduced (6.0f).toNearestInt(),
                              juce::Justification::bottomLeft, 2, 0.8f);
        }
    }
}

//==============================================================================
int SinglePad::noteAt (juce::Point<float> p) const
{
    auto* tr = track();
    if (tr == nullptr || ! getLocalBounds().toFloat().contains (p))
        return -1;
    return drumBaseNote + tr->perf.selectedPad;
}

float SinglePad::velocityAt (juce::Point<float> p, int) const
{
    // PAD-2: higher on the pad plays louder.
    return juce::jlimit (0.05f, 1.0f, 1.0f - p.y / (float) juce::jmax (1, getHeight()));
}

void SinglePad::paint (juce::Graphics& g)
{
    const auto& t = Theme::get();
    auto* tr = track();
    if (tr == nullptr)
        return;

    const int note = drumBaseNote + tr->perf.selectedPad;
    const auto colour = Theme::trackColour (tr->colour);
    auto bounds = getLocalBounds().toFloat().reduced (3.0f);

    juce::ColourGradient gradient (colour.withMultipliedBrightness (isLit (note) ? 1.0f : 0.75f), bounds.getX(), bounds.getY(),
                                   colour.withMultipliedBrightness (isLit (note) ? 0.6f : 0.25f), bounds.getX(), bounds.getBottom(),
                                   false);
    g.setGradientFill (gradient);
    g.fillRoundedRectangle (bounds, 10.0f);

    g.setColour (t.text);
    g.setFont (Theme::font (16.0f, true));
    g.drawText (tr->pads[(size_t) tr->perf.selectedPad].name, bounds.reduced (12.0f), juce::Justification::bottomLeft);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.setFont (Theme::font (12.0f));
    g.drawText ("Louder", bounds.reduced (12.0f), juce::Justification::topRight);
    g.drawText ("Softer", bounds.reduced (12.0f), juce::Justification::bottomRight);
}

//==============================================================================
int MelodicPadGrid::noteForCell (const Set& set, const Track& track, AppSettings::MelodicLayout layout, int row, int column)
{
    const int base = melodicBaseNote (set, track);

    if (! set.inKey)
    {
        // Chromatic: Fourths moves up 5 semitones per row; Sequential continues row by row.
        const int note = layout == AppSettings::MelodicLayout::fourths ? base + row * 5 + column : base + row * 5 + column;
        return juce::jlimit (0, 127, note);
    }

    // In Key: pads hold scale degrees only (PAD-5).
    const auto& intervals = getScales()[(size_t) juce::jlimit (0, (int) getScales().size() - 1, set.scale)].intervals;
    const int size = (int) intervals.size();

    int degreesPerRow = 5;
    if (layout == AppSettings::MelodicLayout::fourths)
    {
        degreesPerRow = 0;
        for (auto interval : intervals)
            if (interval < 5)
                ++degreesPerRow;
    }

    const int degree = row * degreesPerRow + column;
    const int octave = degree / size;
    const int note = base + octave * 12 + intervals[(size_t) (degree % size)];
    return juce::jlimit (0, 127, note);
}

juce::Rectangle<float> MelodicPadGrid::cellBounds (int row, int column) const
{
    const float w = getWidth() / 5.0f, h = getHeight() / 5.0f;
    return juce::Rectangle<float> (column * w, (4 - row) * h, w, h).reduced (2.5f);
}

int MelodicPadGrid::noteAt (juce::Point<float> p) const
{
    auto* tr = track();
    if (tr == nullptr || ! getLocalBounds().toFloat().contains (p))
        return -1;
    const int column = juce::jlimit (0, 4, (int) (p.x / (getWidth() / 5.0f)));
    const int row = 4 - juce::jlimit (0, 4, (int) (p.y / (getHeight() / 5.0f)));
    return noteForCell (studio.getSet(), *tr, studio.getSettings().getMelodicLayout(), row, column);
}

void MelodicPadGrid::paint (juce::Graphics& g)
{
    const auto& t = Theme::get();
    auto* tr = track();
    if (tr == nullptr)
        return;

    const auto& set = studio.getSet();
    const auto colour = Theme::trackColour (tr->colour);
    const auto layout = studio.getSettings().getMelodicLayout();
    const bool showNames = studio.getSettings().getShowNoteNames();
    const int root = set.inKey ? set.root : 0;

    for (int row = 0; row < 5; ++row)
        for (int column = 0; column < 5; ++column)
        {
            const int note = noteForCell (set, *tr, layout, row, column);
            auto bounds = cellBounds (row, column);
            const bool isRoot = ((note - root) % 12 + 12) % 12 == 0;
            const bool inScale = ! set.inKey || isInScale (note, set.root, set.scale);
            const bool lit = isLit (note);

            juce::Colour fill = isRoot ? colour.withMultipliedBrightness (0.7f)
                                       : (inScale ? t.surfaceRaised : t.surface);
            if (lit) fill = colour.brighter (0.25f);

            g.setColour (fill);
            g.fillRoundedRectangle (bounds, 7.0f);

            if (showNames || isRoot)
            {
                g.setColour (lit || isRoot ? juce::Colours::black.withAlpha (0.7f) : t.textDim);
                g.setFont (Theme::font (12.0f, isRoot));
                g.drawText (noteName (note), bounds.reduced (5.0f), juce::Justification::bottomLeft);
            }
        }
}

//==============================================================================
std::vector<KeysView::Key> KeysView::layoutKeys() const
{
    std::vector<Key> keys;
    auto* tr = track();
    if (tr == nullptr)
        return keys;

    const auto& set = studio.getSet();
    const int base = melodicBaseNote (set, *tr);
    auto bounds = getLocalBounds().toFloat().reduced (2.0f);

    if (set.inKey)
    {
        // Equal keys for each scale note across two octaves, plus the top root.
        std::vector<int> notes;
        for (int n = base; n <= base + 24; ++n)
            if (isInScale (n, set.root, set.scale))
                notes.push_back (n);

        const float w = bounds.getWidth() / (float) juce::jmax (1, (int) notes.size());
        for (size_t i = 0; i < notes.size(); ++i)
            keys.push_back ({ notes[i], juce::Rectangle<float> (bounds.getX() + w * i, bounds.getY(), w, bounds.getHeight()).reduced (1.5f), false });
        return keys;
    }

    // Piano: two octaves of white keys from C, black keys on top.
    const int start = base - (((base % 12) + 12) % 12); // C at or below the base
    const bool isBlack[] = { false, true, false, true, false, false, true, false, true, false, true, false };
    int whiteCount = 0;
    for (int n = start; n <= start + 24; ++n)
        if (! isBlack[n % 12])
            ++whiteCount;

    const float whiteWidth = bounds.getWidth() / (float) whiteCount;
    std::vector<Key> blacks;
    int white = 0;
    for (int n = start; n <= start + 24; ++n)
    {
        if (isBlack[n % 12])
        {
            const float x = bounds.getX() + white * whiteWidth - whiteWidth * 0.32f;
            blacks.push_back ({ n, juce::Rectangle<float> (x, bounds.getY(), whiteWidth * 0.64f, bounds.getHeight() * 0.6f), true });
        }
        else
        {
            keys.push_back ({ n, juce::Rectangle<float> (bounds.getX() + white * whiteWidth, bounds.getY(), whiteWidth, bounds.getHeight()).reduced (1.0f), false });
            ++white;
        }
    }

    // Black keys last so they are drawn on top and hit-tested first.
    keys.insert (keys.end(), blacks.begin(), blacks.end());
    return keys;
}

int KeysView::noteAt (juce::Point<float> p) const
{
    auto keys = layoutKeys();
    for (auto it = keys.rbegin(); it != keys.rend(); ++it)
        if (it->bounds.contains (p))
            return it->note;
    return -1;
}

void KeysView::paint (juce::Graphics& g)
{
    const auto& t = Theme::get();
    auto* tr = track();
    if (tr == nullptr)
        return;

    const auto colour = Theme::trackColour (tr->colour);
    const auto& set = studio.getSet();
    const bool showNames = studio.getSettings().getShowNoteNames();

    for (auto& key : layoutKeys())
    {
        const bool lit = isLit (key.note);
        const bool isRoot = (((key.note - (set.inKey ? set.root : 0)) % 12) + 12) % 12 == 0;

        juce::Colour fill = key.black ? juce::Colour (0xff202226) : (set.inKey ? t.surfaceRaised : juce::Colour (0xffdedfe3));
        if (set.inKey && isRoot) fill = colour.withMultipliedBrightness (0.7f);
        if (lit) fill = colour.brighter (0.2f);

        g.setColour (fill);
        g.fillRoundedRectangle (key.bounds, key.black ? 4.0f : 6.0f);

        if (! key.black && (showNames || isRoot))
        {
            g.setColour (set.inKey && ! lit && ! isRoot ? t.textDim : juce::Colours::black.withAlpha (0.6f));
            g.setFont (Theme::font (11.0f));
            g.drawText (noteName (key.note), key.bounds.reduced (2.0f, 6.0f), juce::Justification::centredBottom);
        }
    }
}

} // namespace rb
