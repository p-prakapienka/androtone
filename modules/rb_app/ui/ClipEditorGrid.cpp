namespace rb
{

namespace
{
    constexpr int braceHeight = 24;
    constexpr int rulerHeight = 18;
    constexpr float moveThreshold = 8.0f;
    constexpr double maxPixelsPerBeat = 480.0;
    constexpr float defaultVelocity = 0.8f;

    bool isBlackKey (int pitch)
    {
        switch (((pitch % 12) + 12) % 12)
        {
            case 1: case 3: case 6: case 8: case 10: return true;
            default: return false;
        }
    }

    bool isIn (const Selection& s, int index)
    {
        return std::find (s.begin(), s.end(), index) != s.end();
    }
}

//==============================================================================
ClipEditorGrid::ClipEditorGrid (ClipEditor& o) : owner (o)
{
    setOpaque (true);
}

void ClipEditorGrid::refresh()
{
    auto* tr = owner.track();
    if (tr != nullptr)
    {
        const auto& set = owner.getStudio().getSet();
        auto newRows = editorRows (tr->isDrum(), set.inKey, set.root, set.scale);
        if (newRows != rows)
        {
            rows = std::move (newRows);
            if (! tr->isDrum())
                owner.state().scrollY = -1.0; // re-centre on the notes
            resized();
        }
        else
        {
            ensureViewInitialised(); // e.g. another slot: fit its loop
            clampView();
        }
    }
    repaint();
}

void ClipEditorGrid::resized()
{
    auto* tr = owner.track();
    const bool drum = tr != nullptr && tr->isDrum();
    labelWidth = drum ? 68 : 40;

    const auto oldGrid = gridArea;
    auto bounds = getLocalBounds();
    auto top = bounds.removeFromTop (braceHeight + rulerHeight);
    labelArea = bounds.removeFromLeft (labelWidth);
    gridArea = bounds;
    top.removeFromLeft (labelWidth);
    braceArea = top.removeFromTop (braceHeight);
    rulerArea = top;

    const int numRows = juce::jmax (1, (int) rows.size());
    rowH = drum ? juce::jmax (22.0f, (float) gridArea.getHeight() / (float) numRows) : 30.0f;
    if (! drum && (float) gridArea.getHeight() / 30.0f > (float) numRows)
        rowH = (float) gridArea.getHeight() / (float) numRows;

    // Rotation or the menu opening: keep the same beats visible, and the selection in view.
    auto& st = owner.state();
    if (! oldGrid.isEmpty() && ! gridArea.isEmpty() && st.pixelsPerBeat > 0.0 && oldGrid.getWidth() != gridArea.getWidth())
    {
        st.pixelsPerBeat *= gridArea.getWidth() / (double) oldGrid.getWidth();
        if (st.scrollY >= 0.0)
            st.scrollY += (oldGrid.getHeight() - gridArea.getHeight()) * 0.5; // rotation: keep the centre row
    }

    ensureViewInitialised();
    clampView();

    if (auto* c = owner.clip(); c != nullptr && st.scrollY >= 0.0 && ! oldGrid.isEmpty())
    {
        float lowest = -1.0f;
        for (auto i : owner.getSelection())
            if (i >= 0 && i < (int) c->notes.size())
                lowest = juce::jmax (lowest, noteBounds (c->notes[(size_t) i]).getBottom());
        if (lowest > (float) gridArea.getBottom())
        {
            st.scrollY += lowest - (float) gridArea.getBottom() + rowH;
            clampView();
        }
    }
    repaint();
}

//==============================================================================
double ClipEditorGrid::getAdaptiveGrid() const
{
    auto& st = owner.state();
    const double ppb = st.pixelsPerBeat > 0.0 ? st.pixelsPerBeat : 90.0;
    return editorGridForZoom (ppb, st.triplets);
}

double ClipEditorGrid::contentBeats() const
{
    auto* c = owner.clip();
    if (c == nullptr)
        return beatsPerBar;
    // One spare bar after the clip so the loop brace can be dragged out to grow it.
    return juce::jmin ((double) maxClipBars * beatsPerBar, juce::jmax (c->lengthBeats(), c->loopEnd()) + beatsPerBar);
}

void ClipEditorGrid::ensureViewInitialised()
{
    auto* c = owner.clip();
    if (c == nullptr || gridArea.getWidth() <= 0)
        return;

    auto& st = owner.state();
    if (st.pixelsPerBeat <= 0.0)
    {
        st.pixelsPerBeat = (gridArea.getWidth() - 14.0) / juce::jmax (1.0, c->loopLength);
        st.scrollBeat = c->loopStart;
    }

    if (st.scrollY < 0.0 && ! rows.empty())
    {
        // Centre on the notes (or around C3 / the selected pad when there are none).
        int lo = 128, hi = -1;
        for (auto& n : c->notes)
        {
            lo = juce::jmin (lo, n.pitch);
            hi = juce::jmax (hi, n.pitch);
        }
        if (hi < 0)
            lo = hi = owner.isDrumClip() ? drumBaseNote : 60;

        const int centrePitch = (lo + hi) / 2;
        int centreRow = 0;
        for (int i = 0; i < (int) rows.size(); ++i)
            if (rows[(size_t) i] <= centrePitch)
                centreRow = i;

        const double displayIndex = (double) rows.size() - 1 - centreRow;
        st.scrollY = displayIndex * rowH + rowH * 0.5 - gridArea.getHeight() * 0.5;
    }
}

void ClipEditorGrid::clampView()
{
    auto& st = owner.state();
    if (gridArea.getWidth() <= 0)
        return;

    const double content = contentBeats();
    const double minPpb = gridArea.getWidth() / content;
    st.pixelsPerBeat = juce::jlimit (minPpb, juce::jmax (minPpb, maxPixelsPerBeat), st.pixelsPerBeat);
    const double visibleBeats = gridArea.getWidth() / st.pixelsPerBeat;
    st.scrollBeat = juce::jlimit (0.0, juce::jmax (0.0, content - visibleBeats), st.scrollBeat);

    const double contentHeight = rows.size() * (double) rowH;
    st.scrollY = juce::jlimit (0.0, juce::jmax (0.0, contentHeight - gridArea.getHeight()), juce::jmax (0.0, st.scrollY));
}

void ClipEditorGrid::zoomToRange (double startBeat, double lengthBeats)
{
    auto& st = owner.state();
    if (gridArea.getWidth() <= 0)
    {
        st.pixelsPerBeat = 0.0;
        return;
    }
    // A small margin on the right keeps the loop brace's end handle clear of the screen edge.
    st.pixelsPerBeat = (gridArea.getWidth() - 14.0) / juce::jmax (1.0, lengthBeats);
    st.scrollBeat = startBeat;
    clampView();
    repaint();
}

float ClipEditorGrid::beatToX (double beat) const
{
    auto& st = owner.state();
    return (float) gridArea.getX() + (float) ((beat - st.scrollBeat) * st.pixelsPerBeat);
}

double ClipEditorGrid::xToBeat (float x) const
{
    auto& st = owner.state();
    return st.scrollBeat + (x - (float) gridArea.getX()) / juce::jmax (1.0e-3, st.pixelsPerBeat);
}

float ClipEditorGrid::rowTop (int rowIndex) const
{
    const int displayIndex = (int) rows.size() - 1 - rowIndex;
    return (float) gridArea.getY() + (float) displayIndex * rowH - (float) owner.state().scrollY;
}

int ClipEditorGrid::rowAt (float y) const
{
    if (rows.empty())
        return 0;
    const int displayIndex = (int) std::floor ((y - (float) gridArea.getY() + (float) owner.state().scrollY) / rowH);
    return juce::jlimit (0, (int) rows.size() - 1, (int) rows.size() - 1 - displayIndex);
}

juce::Rectangle<float> ClipEditorGrid::noteBounds (const Note& n) const
{
    const int row = rowIndexOf (rows, n.pitch);
    if (row < 0)
        return {};
    const float x0 = beatToX (n.start);
    const float x1 = beatToX (n.start + n.length);
    return { x0, rowTop (row) + 1.0f, juce::jmax (3.0f, x1 - x0 - 1.0f), rowH - 2.0f };
}

juce::Rectangle<float> ClipEditorGrid::velocityHandle (const Note& n) const
{
    auto r = noteBounds (n);
    if (r.isEmpty())
        return {};
    const float size = 12.0f;
    const float cx = juce::jmax (r.getX() + size * 0.5f, r.getRight() - size * 0.5f - 2.0f);
    return juce::Rectangle<float> (size, size).withCentre ({ cx, r.getY() + 1.0f });
}

ClipEditorGrid::Region ClipEditorGrid::regionAt (juce::Point<float> p) const
{
    if (p.y < (float) braceArea.getBottom() && p.x >= (float) gridArea.getX())
        return Region::brace;
    if (p.y < (float) gridArea.getY())
        return Region::ruler;
    if (p.x < (float) gridArea.getX())
        return Region::labels;
    return Region::grid;
}

float ClipEditorGrid::playheadX() const
{
    auto* c = owner.clip();
    auto& studio = owner.getStudio();
    if (c == nullptr || studio.getPlayingSlot (owner.getTrackId()) != owner.getSlot())
        return -1.0f;
    const float x = beatToX (studio.getClipPosition (owner.getTrackId()));
    return x >= (float) gridArea.getX() && x <= (float) gridArea.getRight() ? x : -1.0f;
}

void ClipEditorGrid::updatePlayhead()
{
    const float x = playheadX();
    if (std::abs (x - lastPlayhead) < 0.5f)
        return;
    for (float px : { lastPlayhead, x })
        if (px >= 0.0f)
            repaint (juce::Rectangle<int> ((int) px - 2, 0, 5, getHeight()));
    lastPlayhead = x;
}

//==============================================================================
void ClipEditorGrid::paint (juce::Graphics& g)
{
    const auto& t = Theme::get();
    g.fillAll (t.surface);

    auto* tr = owner.track();
    auto* c = owner.clip();
    if (tr == nullptr || c == nullptr || rows.empty())
        return;

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (gridArea);
        paintRows (g, *tr, *c);
        paintNotes (g, *tr, *c);
    }

    paintRuler (g, *tr, *c);
    paintLabels (g, *tr);

    const float x = playheadX();
    lastPlayhead = x;
    if (x >= 0.0f)
    {
        g.setColour (t.text);
        g.fillRect (x - 0.75f, (float) rulerArea.getY(), 1.5f, (float) (gridArea.getBottom() - rulerArea.getY()));
    }
}

void ClipEditorGrid::paintRows (juce::Graphics& g, const Track& tr, const Clip& c)
{
    const auto& t = Theme::get();
    const auto& set = owner.getStudio().getSet();
    const auto area = gridArea.toFloat();

    // Rows.
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        const float y = rowTop (i);
        if (y > area.getBottom() || y + rowH < area.getY())
            continue;

        const int pitch = rows[(size_t) i];
        auto colour = t.background.brighter (0.06f);
        if (tr.isDrum())
            colour = (i % 2 == 0) ? t.background.brighter (0.07f) : t.background.brighter (0.03f);
        else if (set.inKey)
            colour = ((pitch - set.root) % 12 + 12) % 12 == 0 ? t.surfaceRaised.withAlpha (0.9f) : t.background.brighter (0.06f);
        else if (isBlackKey (pitch))
            colour = t.background.darker (0.3f);

        if (tr.isDrum() && pitch - drumBaseNote == tr.perf.selectedPad)
            colour = colour.brighter (0.08f);

        g.setColour (colour);
        g.fillRect (area.getX(), y, area.getWidth(), rowH);
        g.setColour (t.background.darker (0.4f));
        g.fillRect (area.getX(), y + rowH - 1.0f, area.getWidth(), 1.0f);

        if (! tr.isDrum() && ! set.inKey && ((pitch % 12) + 12) % 12 == 0)
        {
            g.setColour (t.outline.withAlpha (0.6f));
            g.fillRect (area.getX(), y + rowH - 1.0f, area.getWidth(), 1.0f);
        }
    }

    // Columns: grid cells, beats and bars.
    const double visibleStart = xToBeat (area.getX());
    const double visibleEnd = xToBeat (area.getRight());
    const double cell = owner.getCellGrid();
    const double ppb = owner.state().pixelsPerBeat;

    if (cell * ppb >= 6.0)
        for (double b = floorToGrid (visibleStart, cell); b <= visibleEnd; b += cell)
        {
            g.setColour (t.outline.withAlpha (0.22f));
            g.fillRect (beatToX (b), area.getY(), 1.0f, area.getHeight());
        }

    for (double b = std::floor (visibleStart); b <= visibleEnd; b += 1.0)
    {
        const bool bar = std::fmod (b, (double) beatsPerBar) < 1.0e-6;
        g.setColour (t.outline.withAlpha (bar ? 0.95f : 0.45f));
        g.fillRect (beatToX (b) - (bar ? 0.5f : 0.0f), area.getY(), bar ? 1.5f : 1.0f, area.getHeight());
    }

    // Outside the loop is dimmed, past the clip end more so.
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    if (c.loopStart > visibleStart)
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (area.getX(), area.getY(), beatToX (c.loopStart), area.getBottom()));
    if (c.loopEnd() < visibleEnd)
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmax (area.getX(), beatToX (c.loopEnd())), area.getY(),
                                                               area.getRight(), area.getBottom()));
    if (c.lengthBeats() < visibleEnd)
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmax (area.getX(), beatToX (c.lengthBeats())), area.getY(),
                                                               area.getRight(), area.getBottom()));
    }
}

void ClipEditorGrid::paintNotes (juce::Graphics& g, const Track& tr, const Clip& c)
{
    const auto& t = Theme::get();
    const auto colour = Theme::trackColour (tr.colour);
    const auto area = gridArea.toFloat();
    const double visibleStart = xToBeat (area.getX());
    const double visibleEnd = xToBeat (area.getRight());
    const auto& selection = owner.getSelection();

    std::vector<bool> selected (c.notes.size(), false);
    for (auto i : selection)
        if (i >= 0 && i < (int) selected.size())
            selected[(size_t) i] = true;

    for (size_t i = 0; i < c.notes.size(); ++i)
    {
        auto& n = c.notes[i];
        if (n.start > visibleEnd || n.start + n.length < visibleStart)
            continue;
        auto r = noteBounds (n);
        if (r.isEmpty() || r.getBottom() < area.getY() || r.getY() > area.getBottom())
            continue;

        const bool inLoop = n.start >= c.loopStart && n.start < c.loopEnd();
        auto fill = colour.withMultipliedSaturation (0.2f + 0.8f * n.velocity).withMultipliedBrightness (0.55f + 0.45f * n.velocity);
        if (! inLoop)
            fill = fill.withMultipliedAlpha (0.45f);
        if (selected[i])
            fill = fill.brighter (0.25f);

        g.setColour (fill);
        g.fillRoundedRectangle (r, 3.0f);

        if (selected[i])
        {
            g.setColour (t.text);
            g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 2.0f);
        }
        else
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        }

        // Drum hits with a pitch offset show it (EDIT-10 Pitches).
        if (tr.isDrum() && std::abs (n.pitchOffset) > 0.01f && r.getWidth() > 22.0f)
        {
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.setFont (Theme::font (10.0f, true));
            g.drawText ((n.pitchOffset > 0 ? "+" : "") + juce::String (juce::roundToInt (n.pitchOffset)), r,
                        juce::Justification::centred);
        }
    }

    // Velocity handles on top of all notes (EDIT-9).
    for (auto i : selection)
    {
        if (i < 0 || i >= (int) c.notes.size())
            continue;
        auto h = velocityHandle (c.notes[(size_t) i]);
        if (h.isEmpty() || ! area.intersects (h))
            continue;
        g.setColour (t.text);
        g.fillEllipse (h);
        g.setColour (colour.darker (0.4f));
        g.drawEllipse (h.reduced (1.0f), 1.5f);
    }

    // Pending (uncommitted) notes as outlines (CAP-2, CAP-5).
    auto& studio = owner.getStudio();
    for (auto& n : studio.getPendingNotes (owner.getTrackId(), owner.getSlot()))
    {
        auto r = noteBounds (n);
        if (r.isEmpty() || ! area.intersects (r))
            continue;
        g.setColour (t.text.withAlpha (0.12f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (t.text.withAlpha (0.8f));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.2f);
    }

    // Gesture feedback.
    if (mode == Mode::holdEmpty)
    {
        const double cell = owner.getCellGrid();
        const double start = floorToGrid (downBeat, cell);
        const float y = rowTop (downRow);
        juce::Rectangle<float> ghost (beatToX (start), y + 1.0f, (float) (cell * owner.state().pixelsPerBeat) - 1.0f, rowH - 2.0f);
        g.setColour (t.accent.withAlpha (0.35f));
        g.fillRoundedRectangle (ghost, 3.0f);
        g.setColour (t.accent);
        g.drawRoundedRectangle (ghost, 3.0f, 1.5f);
    }

    if (mode == Mode::selectBox && ! selectionBox.isEmpty())
    {
        g.setColour (t.accent.withAlpha (0.15f));
        g.fillRect (selectionBox);
        g.setColour (t.accent);
        g.drawRect (selectionBox, 1.5f);
    }

    if (mode == Mode::velocity && anchorIndex >= 0)
    {
        // Show the anchor note's velocity while dragging.
        const auto& sel = owner.getSelection();
        for (auto i : sel)
        {
            if (i < 0 || i >= (int) c.notes.size())
                continue;
            auto h = velocityHandle (c.notes[(size_t) i]);
            if (h.getCentre().getDistanceFrom (lastPos) > 80.0f && sel.size() > 1)
                continue;
            juce::Rectangle<float> label (64.0f, 22.0f);
            label = label.withCentre ({ h.getCentreX(), h.getY() - 16.0f });
            if (label.getY() < area.getY())
                label = label.withY (h.getBottom() + 4.0f);
            g.setColour (t.surfaceRaised);
            g.fillRoundedRectangle (label, 5.0f);
            g.setColour (t.text);
            g.setFont (Theme::font (12.0f, true));
            g.drawText ("Vel " + juce::String (juce::roundToInt (c.notes[(size_t) i].velocity * 127.0f)), label,
                        juce::Justification::centred);
            break;
        }
    }
}

void ClipEditorGrid::paintRuler (juce::Graphics& g, const Track& tr, const Clip& c)
{
    const auto& t = Theme::get();
    const auto colour = Theme::trackColour (tr.colour);

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (juce::Rectangle<int> (gridArea.getX(), 0, gridArea.getWidth(), gridArea.getY()));

    g.setColour (t.surface);
    g.fillRect (braceArea.getUnion (rulerArea));

    // Clip extent and the loop brace (EDIT-12).
    const float braceY = (float) braceArea.getY() + 6.0f;
    const float braceH = (float) braceArea.getHeight() - 10.0f;
    g.setColour (t.surfaceRaised);
    g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (beatToX (0.0), braceY + 3.0f, beatToX (c.lengthBeats()),
                                                                        braceY + braceH - 3.0f), 3.0f);

    auto loop = juce::Rectangle<float>::leftTopRightBottom (beatToX (c.loopStart), braceY, beatToX (c.loopEnd()), braceY + braceH);
    g.setColour (colour.withAlpha (0.85f));
    g.fillRoundedRectangle (loop, 4.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    for (float x : { loop.getX() + 6.0f, loop.getRight() - 6.0f })
        g.fillRoundedRectangle (x - 1.5f, loop.getY() + 3.0f, 3.0f, loop.getHeight() - 6.0f, 1.5f);

    // Bar and beat numbers.
    const double ppb = owner.state().pixelsPerBeat;
    const double visibleStart = xToBeat ((float) gridArea.getX());
    const double visibleEnd = xToBeat ((float) gridArea.getRight());
    const bool showBeats = ppb >= 44.0;
    const int barStep = ppb * beatsPerBar >= 36.0 ? 1 : (ppb * beatsPerBar >= 18.0 ? 2 : 4);

    for (double b = std::floor (visibleStart); b <= visibleEnd; b += 1.0)
    {
        const int beat = (int) b;
        const bool bar = beat % beatsPerBar == 0;
        const int barIndex = beat / beatsPerBar;
        const float x = beatToX (b);

        if (bar && barIndex % barStep == 0)
        {
            g.setColour (b < c.lengthBeats() ? t.text : t.textDim);
            g.setFont (Theme::font (12.0f, true));
            g.drawText (juce::String (barIndex + 1), juce::Rectangle<float> (x + 3.0f, (float) rulerArea.getY(), 30.0f, (float) rulerArea.getHeight()),
                        juce::Justification::centredLeft);
            g.setColour (t.outline);
            g.fillRect (x - 0.5f, (float) rulerArea.getY() + 2.0f, 1.5f, (float) rulerArea.getHeight() - 2.0f);
        }
        else if (! bar && showBeats)
        {
            g.setColour (t.textDim);
            g.setFont (Theme::font (10.0f));
            g.drawText (juce::String (barIndex + 1) + "." + juce::String (beat % beatsPerBar + 1),
                        juce::Rectangle<float> (x + 3.0f, (float) rulerArea.getY(), 30.0f, (float) rulerArea.getHeight()),
                        juce::Justification::centredLeft);
            g.setColour (t.outline.withAlpha (0.6f));
            g.fillRect (x, (float) rulerArea.getBottom() - 6.0f, 1.0f, 6.0f);
        }
    }

    g.setColour (t.outline.withAlpha (0.8f));
    g.fillRect (gridArea.getX(), gridArea.getY() - 1, gridArea.getWidth(), 1);
}

void ClipEditorGrid::paintLabels (juce::Graphics& g, const Track& tr)
{
    const auto& t = Theme::get();
    const auto& set = owner.getStudio().getSet();

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (labelArea);
        g.setColour (t.surface);
        g.fillRect (labelArea);

        for (int i = 0; i < (int) rows.size(); ++i)
        {
            const float y = rowTop (i);
            if (y > (float) labelArea.getBottom() || y + rowH < (float) labelArea.getY())
                continue;

            const int pitch = rows[(size_t) i];
            auto r = juce::Rectangle<float> ((float) labelArea.getX(), y, (float) labelArea.getWidth(), rowH);

            if (tr.isDrum())
            {
                const int pad = pitch - drumBaseNote;
                const bool selectedPad = pad == tr.perf.selectedPad;
                if (selectedPad)
                {
                    g.setColour (Theme::trackColour (tr.colour).withAlpha (0.3f));
                    g.fillRect (r.reduced (0.0f, 1.0f));
                }
                g.setColour (selectedPad ? t.text : t.textDim);
                g.setFont (Theme::font (juce::jmin (12.0f, rowH * 0.5f), selectedPad));
                const auto name = tr.pads[(size_t) juce::jlimit (0, numDrumPads - 1, pad)].name;
                g.drawFittedText (name.isNotEmpty() ? name : "Pad " + juce::String (pad + 1),
                                  r.reduced (5.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
            }
            else
            {
                const bool black = ! set.inKey && isBlackKey (pitch);
                const bool isRoot = set.inKey ? ((pitch - set.root) % 12 + 12) % 12 == 0 : pitch % 12 == 0;
                g.setColour (black ? t.background.darker (0.3f) : t.surfaceRaised.withAlpha (0.6f));
                g.fillRect (r.reduced (0.0f, 0.5f).withTrimmedRight (2.0f));
                g.setColour (isRoot ? t.text : t.textDim);
                g.setFont (Theme::font (juce::jmin (11.5f, rowH * 0.45f), isRoot));
                g.drawText (noteName (pitch), r.reduced (4.0f, 0.0f), juce::Justification::centredLeft);
            }
        }
    }

    // Corner above the labels: the current grid value.
    auto corner = juce::Rectangle<int> (labelArea.getX(), 0, labelArea.getWidth(), gridArea.getY());
    g.setColour (t.surface);
    g.fillRect (corner);
    const double cell = owner.getCellGrid();
    juce::String label = owner.state().gridChoice == 0 ? "Off" : juce::String();
    if (label.isEmpty())
        for (auto& d : getGridDivisions())
            if (std::abs (d.beats - cell) < 1.0e-6 || std::abs (tripletOf (d.beats) - cell) < 1.0e-6)
                label = juce::String (d.label) + (std::abs (d.beats - cell) < 1.0e-6 ? "" : "T");
    g.setColour (t.textDim);
    g.setFont (Theme::font (11.0f));
    g.drawText (label, corner.withTrimmedTop (braceHeight), juce::Justification::centred);
}

//==============================================================================
void ClipEditorGrid::startGesture (Mode m)
{
    mode = m;
    if (! gestureOpen)
    {
        owner.beginGesture();
        gestureOpen = true;
    }
}

void ClipEditorGrid::finishGesture()
{
    stopTimer();
    if (gestureOpen)
    {
        owner.endGesture();
        gestureOpen = false;
    }
    mode = Mode::none;
    primaryTouch = -1;
    baseClip = nullptr;
    anchorIndex = -1;
    selectionBox = {};
    lastAuditionPitch = -1;
    owner.setSelection (owner.getSelection()); // lets the note-edit menu follow the final selection
    repaint();
}

void ClipEditorGrid::mouseDown (const juce::MouseEvent& e)
{
    const int index = e.source.getIndex();
    touches[index] = e.position;

    if (touches.size() >= 2)
    {
        // A second finger turns an undecided touch into pinch-zoom / two-finger scroll (EDIT-4).
        if (mode == Mode::none || mode == Mode::pending || mode == Mode::scroll || mode == Mode::holdEmpty
            || mode == Mode::rulerPending || mode == Mode::rulerScroll || mode == Mode::labels)
            startPinch();
        return;
    }

    auto c = owner.clipPtr();
    if (c == nullptr || rows.empty())
        return;

    primaryTouch = index;
    downPos = lastPos = e.position;
    downRegion = regionAt (e.position);
    downBeat = xToBeat (e.position.x);
    downRow = rowAt (e.position.y);
    downHit = {};
    downOnSelected = false;
    anchorIndex = -1;
    baseClip = c;
    baseSelection = owner.getSelection();
    lastApplied = {};
    lastAuditionPitch = -1;

    auto& st = owner.state();
    baseScrollBeat = st.scrollBeat;
    baseScrollY = st.scrollY;
    basePixelsPerBeat = st.pixelsPerBeat;

    switch (downRegion)
    {
        case Region::grid:
        {
            // A selected note's velocity handle wins over everything under it.
            for (auto i : baseSelection)
                if (i >= 0 && i < (int) c->notes.size()
                    && velocityHandle (c->notes[(size_t) i]).expanded (10.0f).contains (e.position))
                {
                    anchorIndex = i;
                    downHit.index = i;
                    downOnSelected = true;
                    mode = Mode::pending;
                    startTimer (450);
                    return;
                }

            downHit = hitTestNote (*c, downBeat, rows[(size_t) downRow], 14.0 / juce::jmax (1.0, st.pixelsPerBeat));
            downOnSelected = downHit.index >= 0 && isIn (baseSelection, downHit.index);
            mode = Mode::pending;
            startTimer (450);
            break;
        }

        case Region::brace:
        {
            const float startX = beatToX (c->loopStart), endX = beatToX (c->loopEnd());
            const float dStart = std::abs (e.position.x - startX), dEnd = std::abs (e.position.x - endX);
            if (juce::jmin (dStart, dEnd) < 18.0f)
            {
                braceEnd = dEnd <= dStart;
                mode = Mode::braceEdge;
                return;
            }
            mode = Mode::rulerPending;
            startTimer (450);
            break;
        }

        case Region::ruler:
            mode = Mode::rulerPending;
            startTimer (450);
            break;

        case Region::labels:
        case Region::corner:
            mode = Mode::labels;
            break;
    }
}

void ClipEditorGrid::mouseDrag (const juce::MouseEvent& e)
{
    touches[e.source.getIndex()] = e.position;

    if (mode == Mode::pinch)
    {
        updatePinch();
        return;
    }
    if (e.source.getIndex() != primaryTouch)
        return;

    dragTo (e.position);
}

void ClipEditorGrid::mouseUp (const juce::MouseEvent& e)
{
    const int index = e.source.getIndex();
    touches.erase (index);

    if (mode == Mode::pinch)
    {
        if (touches.empty())
            finishGesture();
        return;
    }
    if (index != primaryTouch)
        return;

    stopTimer();
    const bool moved = e.position.getDistanceFrom (downPos) >= moveThreshold;

    switch (mode)
    {
        case Mode::pending:
            tap (downPos);
            break;

        case Mode::holdEmpty:
            addNoteAt (downBeat, downRow); // held but never dragged: a single note
            break;

        case Mode::rulerPending:
            if (auto* c = owner.clip())
            {
                // EDIT-12: tap a bar on the ruler to add it to / remove it from the loop.
                const int bar = (int) std::floor (downBeat / beatsPerBar);
                if (downBeat >= 0.0 && (c->lengthBars > 1 || bar >= c->lengthBars))
                {
                    const auto range = toggleLoopBar (*c, bar);
                    if (std::abs (range.start - c->loopStart) > 1.0e-9 || std::abs (range.length - c->loopLength) > 1.0e-9)
                        owner.commitClip (setLoop (*c, range.start, range.length), true);
                }
            }
            break;

        case Mode::labels:
            if (! moved)
            {
                // Tap a row label to hear it; drum rows also select the pad.
                const int pitch = rows[(size_t) downRow];
                owner.audition (pitch, defaultVelocity);
                if (owner.isDrumClip())
                {
                    const int id = owner.getTrackId(), pad = pitch - drumBaseNote;
                    owner.getStudio().getDocument().editWithoutUndo ([id, pad] (Set& s)
                    {
                        if (auto* t = s.findTrack (id))
                            t->perf.selectedPad = juce::jlimit (0, numDrumPads - 1, pad);
                    });
                }
            }
            break;

        case Mode::none:
        case Mode::scroll:
        case Mode::pinch:
        case Mode::moveNotes:
        case Mode::resizeSelection:
        case Mode::resizeNote:
        case Mode::velocity:
        case Mode::extendSelection:
        case Mode::draw:
        case Mode::selectBox:
        case Mode::braceEdge:
        case Mode::rulerScroll:
        case Mode::finished:
            break;
    }

    finishGesture();
}

void ClipEditorGrid::timerCallback()
{
    longPressed();
}

void ClipEditorGrid::longPressed()
{
    stopTimer();

    if (mode == Mode::pending)
    {
        if (downHit.index >= 0)
        {
            // EDIT-8: long-press a note to select it, then drag across notes to add them.
            auto sel = owner.getSelection();
            if (! isIn (sel, downHit.index))
                sel.push_back (downHit.index);
            owner.setSelection (sel);
            mode = Mode::extendSelection;
        }
        else
        {
            mode = Mode::holdEmpty; // the first move decides: draw (EDIT-6) or selection box (EDIT-8)
        }
        repaint();
    }
    else if (mode == Mode::rulerPending)
    {
        mode = Mode::finished;
        owner.showLoopMenu (*this, false);
    }
}

void ClipEditorGrid::tap (juce::Point<float>)
{
    if (downRegion != Region::grid)
        return;

    auto* c = owner.clip();
    if (c == nullptr)
        return;

    auto sel = owner.getSelection();
    if (downHit.index >= 0)
    {
        if (! sel.empty())
        {
            // EDIT-8: with a selection, tapping notes adds (or removes) them.
            if (isIn (sel, downHit.index))
                sel.erase (std::remove (sel.begin(), sel.end(), downHit.index), sel.end());
            else
                sel.push_back (downHit.index);
            owner.setSelection (sel);
        }
        else
        {
            owner.commit (deleteNotes (*c, { downHit.index }), false); // EDIT-5
        }
        return;
    }

    if (! sel.empty())
        owner.setSelection ({}); // tapping outside clears the selection
    else
        addNoteAt (downBeat, downRow);
}

void ClipEditorGrid::addNoteAt (double beat, int rowIndex)
{
    auto* c = owner.clip();
    if (c == nullptr || beat < 0.0 || beat >= c->lengthBeats() || rowIndex < 0 || rowIndex >= (int) rows.size())
        return;

    const double cell = owner.getCellGrid();
    Note n;
    n.start = floorToGrid (beat, cell);
    const double remembered = owner.state().lastDrawnLength;
    n.length = owner.isDrumClip() || remembered <= 0.0 ? cell : remembered;
    n.length = juce::jmin (n.length, c->lengthBeats() - n.start);
    n.pitch = rows[(size_t) rowIndex];
    n.velocity = defaultVelocity;

    owner.commit (addNote (*c, n), false);
    owner.audition (n.pitch, n.velocity);
}

int ClipEditorGrid::pitchAfterMove (int pitch, int rowDelta) const
{
    const auto& set = owner.getStudio().getSet();
    if (owner.isDrumClip())
        return juce::jlimit (drumBaseNote, drumBaseNote + numDrumPads - 1, pitch + rowDelta);
    return transposeInScale (pitch, rowDelta, set.inKey, set.root, set.scale);
}

void ClipEditorGrid::dragTo (juce::Point<float> p)
{
    lastPos = p;
    auto& st = owner.state();
    const auto& set = owner.getStudio().getSet();
    const float dx = p.x - downPos.x, dy = p.y - downPos.y;
    const float distance = p.getDistanceFrom (downPos);
    const double ppb = juce::jmax (1.0e-3, st.pixelsPerBeat);
    const bool drum = owner.isDrumClip();

    // Decide what an undecided touch is doing.
    switch (mode)
    {
        case Mode::pending:
            if (distance < moveThreshold)
                return;
            stopTimer();
            if (downOnSelected)
            {
                anchorIndex = downHit.index;
                const bool onHandle = baseClip != nullptr && anchorIndex >= 0
                                   && velocityHandle (baseClip->notes[(size_t) anchorIndex]).expanded (10.0f).contains (downPos);
                startGesture (onHandle ? Mode::velocity : (downHit.rightEdge ? Mode::resizeSelection : Mode::moveNotes));
            }
            else if (downHit.index >= 0 && downHit.rightEdge)
            {
                anchorIndex = downHit.index;
                startGesture (Mode::resizeNote); // EDIT-7
            }
            else
            {
                mode = Mode::scroll;
            }
            break;

        case Mode::holdEmpty:
            if (distance < moveThreshold)
                return;
            if (std::abs (dy) > std::abs (dx))
            {
                mode = Mode::selectBox;
            }
            else
            {
                if (baseClip == nullptr || downBeat < 0.0 || downBeat >= baseClip->lengthBeats())
                {
                    mode = Mode::finished;
                    return;
                }
                startGesture (Mode::draw);
                owner.audition (rows[(size_t) downRow], defaultVelocity);
            }
            break;

        case Mode::rulerPending:
            if (distance < moveThreshold)
                return;
            stopTimer();
            if (downRegion == Region::brace && std::abs (dy) > std::abs (dx))
            {
                // Swipe up on the brace zooms to the loop, down to the whole clip (EDIT-12).
                if (std::abs (dy) < 28.0f)
                    return;
                if (dy < 0) owner.zoomToLoop();
                else        owner.zoomToClip();
                mode = Mode::finished;
                return;
            }
            mode = Mode::rulerScroll;
            break;

        case Mode::none:
        case Mode::scroll:
        case Mode::pinch:
        case Mode::moveNotes:
        case Mode::resizeSelection:
        case Mode::resizeNote:
        case Mode::velocity:
        case Mode::extendSelection:
        case Mode::draw:
        case Mode::selectBox:
        case Mode::braceEdge:
        case Mode::rulerScroll:
        case Mode::labels:
        case Mode::finished:
            break;
    }

    switch (mode)
    {
        case Mode::scroll:
        case Mode::rulerScroll:
        case Mode::labels:
        {
            if (mode != Mode::labels)
                st.scrollBeat = baseScrollBeat - dx / ppb;
            if (mode != Mode::rulerScroll)
                st.scrollY = baseScrollY - dy;
            clampView();
            repaint();
            break;
        }

        case Mode::moveNotes:
        {
            if (baseClip == nullptr || anchorIndex < 0)
                return;
            const auto& anchor = baseClip->notes[(size_t) anchorIndex];
            const double snap = owner.getSnapGrid();
            double newStart = anchor.start + dx / ppb;
            newStart = snap > 0.0 ? snapToGrid (newStart, snap) : std::round (newStart * 128.0) / 128.0;
            newStart = juce::jlimit (0.0, baseClip->lengthBeats() - 1.0 / 128.0, newStart);
            const double deltaBeats = newStart - anchor.start;
            const int rowDelta = juce::roundToInt (-dy / rowH);

            const auto key = juce::String (deltaBeats, 6) + ":" + juce::String (rowDelta);
            if (key == lastApplied)
                return;
            lastApplied = key;

            // EDIT-9: move in time (snapped) and pitch (scale steps when In Key, pads for drums).
            auto result = moveNotes (*baseClip, baseSelection, deltaBeats, 0);
            if (rowDelta != 0)
                result = drum || set.inKey
                             ? transposeNotes (*result.clip, result.selection, rowDelta, drum, set.inKey, set.root, set.scale)
                             : moveNotes (*result.clip, result.selection, 0.0, rowDelta);
            owner.commit (result);

            const int pitch = pitchAfterMove (anchor.pitch, rowDelta);
            if (pitch != lastAuditionPitch)
            {
                if (lastAuditionPitch >= 0)
                    owner.audition (pitch, anchor.velocity);
                lastAuditionPitch = pitch;
            }
            break;
        }

        case Mode::resizeSelection:
        case Mode::resizeNote:
        {
            if (baseClip == nullptr || anchorIndex < 0)
                return;
            const auto& anchor = baseClip->notes[(size_t) anchorIndex];
            const double snap = owner.getSnapGrid();
            const double minLength = snap > 0.0 ? juce::jmin (snap, 0.125) : 1.0 / 32.0;
            const double end = anchor.start + anchor.length + dx / ppb;
            const double newEnd = juce::jmax (anchor.start + minLength, snap > 0.0 ? snapToGrid (end, snap) : end);
            const double delta = newEnd - (anchor.start + anchor.length);

            const auto key = juce::String (delta, 6);
            if (key == lastApplied)
                return;
            lastApplied = key;

            if (mode == Mode::resizeSelection)
                owner.commit (resizeNotes (*baseClip, baseSelection, delta, minLength));
            else
                owner.commit ({ setNoteLength (*baseClip, anchorIndex, newEnd - anchor.start).clip, baseSelection });
            break;
        }

        case Mode::velocity:
        {
            if (baseClip == nullptr)
                return;
            const float delta = -dy / 160.0f;
            std::vector<float> values;
            for (auto i : baseSelection)
                values.push_back (juce::jlimit (0.01f, 1.0f, baseClip->notes[(size_t) i].velocity + delta));
            const auto key = juce::String (juce::roundToInt (delta * 127.0f));
            if (key == lastApplied)
                return;
            lastApplied = key;
            owner.commit (setVelocities (*baseClip, baseSelection, values));
            break;
        }

        case Mode::extendSelection:
        {
            auto* c = owner.clip();
            if (c == nullptr || regionAt (p) != Region::grid)
                return;
            const auto hit = hitTestNote (*c, xToBeat (p.x), rows[(size_t) rowAt (p.y)], 0.0);
            if (hit.index >= 0 && ! isIn (owner.getSelection(), hit.index))
            {
                auto sel = owner.getSelection();
                sel.push_back (hit.index);
                owner.setSelection (sel);
            }
            break;
        }

        case Mode::draw:
        {
            if (baseClip == nullptr)
                return;
            const int pitch = rows[(size_t) downRow];
            const double cell = owner.getCellGrid();
            const double beat = juce::jlimit (0.0, baseClip->lengthBeats() - 1.0e-6, xToBeat (p.x));

            if (drum)
            {
                // EDIT-6: drum rows get consecutive notes.
                auto run = makeDrumRun (pitch, downBeat, beat, cell, defaultVelocity, baseClip->lengthBeats());
                const auto key = juce::String ((int) run.size()) + ":" + juce::String (run.empty() ? 0.0 : run.front().start, 4);
                if (key == lastApplied)
                    return;
                lastApplied = key;
                owner.commit (addNotes (*baseClip, run), false);
            }
            else
            {
                // EDIT-6: melodic rows get one note whose length follows the drag.
                auto n = makeDrawnNote (pitch, downBeat, beat, cell, defaultVelocity, baseClip->lengthBeats());
                const auto key = juce::String (n.start, 4) + ":" + juce::String (n.length, 4);
                if (key == lastApplied)
                    return;
                lastApplied = key;
                st.lastDrawnLength = n.length;
                owner.commit (addNote (*baseClip, n), false);
            }
            break;
        }

        case Mode::selectBox:
        {
            auto* c = owner.clip();
            if (c == nullptr)
                return;
            selectionBox = juce::Rectangle<float> (downPos, p).getIntersection (gridArea.toFloat());
            const int rowA = rowAt (downPos.y), rowB = rowAt (p.y);
            owner.setSelection (selectInRect (*c, xToBeat (selectionBox.getX()), xToBeat (selectionBox.getRight()),
                                              rows[(size_t) juce::jmin (rowA, rowB)], rows[(size_t) juce::jmax (rowA, rowB)]));
            repaint();
            break;
        }

        case Mode::braceEdge:
        {
            if (baseClip == nullptr)
                return;
            const auto range = dragLoopEdge (*baseClip, braceEnd, xToBeat (p.x));
            const auto key = juce::String (range.start, 3) + ":" + juce::String (range.length, 3);
            if (key == lastApplied)
                return;
            lastApplied = key;
            startGesture (Mode::braceEdge);
            owner.commitClip (setLoop (*baseClip, range.start, range.length), true);
            break;
        }

        case Mode::none:
        case Mode::pending:
        case Mode::holdEmpty:
        case Mode::pinch:
        case Mode::rulerPending:
        case Mode::finished:
            break;
    }
}

//==============================================================================
void ClipEditorGrid::startPinch()
{
    if (touches.size() < 2)
        return;
    stopTimer();

    auto it = touches.begin();
    const auto a = it->second;
    const auto b = (++it)->second;

    mode = Mode::pinch;
    selectionBox = {};
    pinchStartDistance = juce::jmax (30.0f, std::abs (a.x - b.x));
    pinchStartMid = (a + b) * 0.5f;

    auto& st = owner.state();
    basePixelsPerBeat = st.pixelsPerBeat;
    baseScrollBeat = st.scrollBeat;
    baseScrollY = st.scrollY;
    repaint();
}

void ClipEditorGrid::updatePinch()
{
    if (touches.size() < 2)
        return;

    auto it = touches.begin();
    const auto a = it->second;
    const auto b = (++it)->second;
    const auto mid = (a + b) * 0.5f;
    const float distance = juce::jmax (30.0f, std::abs (a.x - b.x));

    auto& st = owner.state();
    const double anchorBeat = baseScrollBeat + (pinchStartMid.x - (float) gridArea.getX()) / basePixelsPerBeat;
    st.pixelsPerBeat = basePixelsPerBeat * distance / pinchStartDistance;
    clampView();
    st.scrollBeat = anchorBeat - (mid.x - (float) gridArea.getX()) / st.pixelsPerBeat;
    st.scrollY = baseScrollY - (mid.y - pinchStartMid.y);
    clampView();
    repaint();
}

void ClipEditorGrid::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    auto& st = owner.state();
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        mouseMagnify (e, 1.0f + wheel.deltaY);
        return;
    }

    float dx = wheel.deltaX, dy = wheel.deltaY;
    if (e.mods.isShiftDown())
        std::swap (dx, dy);
    st.scrollBeat -= dx * 200.0 / juce::jmax (1.0e-3, st.pixelsPerBeat);
    st.scrollY -= dy * 200.0;
    clampView();
    repaint();
}

void ClipEditorGrid::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    auto& st = owner.state();
    const double anchorBeat = xToBeat (e.position.x);
    st.pixelsPerBeat *= juce::jlimit (0.5f, 2.0f, scaleFactor);
    clampView();
    st.scrollBeat = anchorBeat - (e.position.x - (float) gridArea.getX()) / st.pixelsPerBeat;
    clampView();
    repaint();
}

} // namespace rb
