namespace rb
{

ClipEditor::ClipEditor (Studio& s, Navigator& n, int id) : studio (s), nav (n), trackId (id)
{
    studio.addListener (this);
    startTimerHz (30);
}

ClipEditor::~ClipEditor()
{
    studio.removeListener (this);
}

void ClipEditor::setSlot (int s)
{
    slot = s;
    repaint();
}

void ClipEditor::setExpanded (bool e)
{
    if (expanded == e)
        return;
    expanded = e;
    if (onExpandedChanged != nullptr)
        onExpandedChanged (expanded);
    repaint();
}

const Clip* ClipEditor::clip() const
{
    auto* track = studio.getSet().findTrack (trackId);
    if (track == nullptr || slot < 0 || slot >= numScenes)
        return nullptr;
    return track->slots[(size_t) slot].get();
}

void ClipEditor::paint (juce::Graphics& g)
{
    const auto& t = Theme::get();
    auto bounds = getLocalBounds().toFloat().reduced (4.0f, 2.0f);
    g.setColour (t.surface);
    g.fillRoundedRectangle (bounds, 8.0f);

    auto* track = studio.getSet().findTrack (trackId);
    auto* c = clip();
    if (track == nullptr)
        return;

    if (c == nullptr)
    {
        g.setColour (t.textDim);
        g.setFont (Theme::font (14.0f));
        drawIcon (g, Icon::plus, bounds.withSizeKeepingCentre (28.0f, 28.0f).translated (0.0f, -10.0f), t.text);
        g.drawText (slot >= 0 ? "Add a clip (1-16 bars), or just play" : "Play the pads, then capture",
                    bounds.withTrimmedTop (bounds.getHeight() * 0.55f), juce::Justification::centredTop);
        return;
    }

    const auto colour = Theme::trackColour (track->colour);
    auto area = bounds.reduced (8.0f, 8.0f);

    int lo = 127, hi = 0;
    const auto pending = studio.getPendingNotes (trackId, slot);
    for (auto* list : { &c->notes, &pending })
        for (auto& n : *list)
        {
            lo = juce::jmin (lo, n.pitch);
            hi = juce::jmax (hi, n.pitch);
        }
    if (lo > hi) { lo = 48; hi = 72; }
    const float rows = (float) (hi - lo + 1);

    // Bar lines.
    g.setColour (t.outline.withAlpha (0.5f));
    for (double b = 0.0; b <= c->loopLength + 1.0e-6; b += beatsPerBar)
    {
        const float x = area.getX() + (float) (b / c->loopLength) * area.getWidth();
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
    }

    auto drawNotes = [&] (const std::vector<Note>& notes, juce::Colour col)
    {
        for (auto& n : notes)
        {
            if (n.start < c->loopStart || n.start >= c->loopEnd())
                continue;
            const float x = area.getX() + (float) ((n.start - c->loopStart) / c->loopLength) * area.getWidth();
            const float w = juce::jmax (2.0f, (float) (n.length / c->loopLength) * area.getWidth());
            const float h = juce::jmax (2.0f, area.getHeight() / rows);
            const float y = area.getBottom() - (n.pitch - lo + 1) * area.getHeight() / rows;
            g.setColour (col.withMultipliedSaturation (0.4f + 0.6f * n.velocity));
            g.fillRoundedRectangle (x, y, w, h - 1.0f, 1.5f);
        }
    };

    drawNotes (c->notes, colour);
    drawNotes (pending, t.text.withAlpha (0.6f));

    if (studio.getPlayingSlot (trackId) == slot)
    {
        const double position = studio.getClipPosition (trackId);
        const float x = area.getX() + (float) ((position - c->loopStart) / c->loopLength) * area.getWidth();
        g.setColour (t.text);
        g.drawVerticalLine ((int) x, bounds.getY(), bounds.getBottom());
    }
}

void ClipEditor::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked())
        return;

    if (clip() == nullptr)
    {
        if (slot < 0)
        {
            auto* track = studio.getSet().findTrack (trackId);
            for (int s = 0; track != nullptr && s < numScenes; ++s)
                if (track->slots[(size_t) s] == nullptr) { slot = s; break; }
            if (slot < 0)
                return;
        }

        juce::PopupMenu menu;
        menu.addSectionHeader ("New clip length");
        for (int bars : { 1, 2, 4, 8, 16 })
            menu.addItem (bars, juce::String (bars) + (bars == 1 ? " bar" : " bars"));

        juce::Component::SafePointer<ClipEditor> safe (this);
        showMenu (menu, *this, [safe] (int bars)
        {
            if (safe != nullptr && bars > 0)
                safe->studio.createFixedLengthClip (safe->trackId, safe->slot, bars);
        });
        return;
    }

    setExpanded (! expanded);
}

} // namespace rb
