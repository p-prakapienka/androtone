namespace rb
{

static std::map<int, ClipEditor::ViewState>& editorStates()
{
    static std::map<int, ClipEditor::ViewState> states;
    return states;
}

static bool sameNote (const Note& a, const Note& b)
{
    return a.pitch == b.pitch && std::abs (a.start - b.start) < 1.0e-9 && std::abs (a.length - b.length) < 1.0e-9
        && std::abs (a.velocity - b.velocity) < 1.0e-6f && std::abs (a.pitchOffset - b.pitchOffset) < 1.0e-6f;
}

//==============================================================================
// Round expand/collapse button; drag it to move it to another corner (EDIT-2).
class ClipEditor::CornerButton : public juce::Component
{
public:
    explicit CornerButton (ClipEditor& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        const auto& t = Theme::get();
        auto bounds = getLocalBounds().toFloat().reduced (3.0f);
        g.setColour (t.surfaceRaised.withAlpha (dragging ? 1.0f : 0.92f));
        g.fillEllipse (bounds);
        g.setColour (t.outline);
        g.drawEllipse (bounds, 1.0f);
        drawIcon (g, owner.expanded ? Icon::collapse : Icon::expand, bounds.reduced (bounds.getWidth() * 0.24f), t.text);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = false;
        downCentre = getBounds().getCentre();
        downPosition = e.getEventRelativeTo (getParentComponent()).getPosition();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto p = e.getEventRelativeTo (getParentComponent()).getPosition();
        if (! dragging && p.getDistanceFrom (downPosition) < 8)
            return;
        dragging = true;
        auto limits = getParentComponent()->getLocalBounds();
        setCentrePosition (juce::jlimit (limits.getX(), limits.getRight(), downCentre.x + p.x - downPosition.x),
                           juce::jlimit (limits.getY(), limits.getBottom(), downCentre.y + p.y - downPosition.y));
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging)
        {
            dragging = false;
            const auto area = owner.cornerArea();
            const auto c = getBounds().getCentre();
            const bool right = c.x > area.getCentreX();
            const bool bottom = c.y > area.getCentreY();
            owner.state().corner = (bottom ? 2 : 0) + (right ? 1 : 0);
            owner.layoutCornerButton();
            repaint();
            return;
        }
        owner.setExpanded (! owner.expanded);
    }

private:
    ClipEditor& owner;
    bool dragging = false;
    juce::Point<int> downCentre, downPosition;
};

//==============================================================================
ClipEditor::ClipEditor (Studio& s, Navigator& n, int id) : studio (s), nav (n), trackId (id)
{
    grid = std::make_unique<ClipEditorGrid> (*this);
    addChildComponent (*grid);
    menu = std::make_unique<NoteEditMenu> (*this);
    addChildComponent (*menu);
    cornerButton = std::make_unique<CornerButton> (*this);
    addAndMakeVisible (*cornerButton);

    for (auto* b : { &undoButton, &redoButton, &menuButton, &duplicateLoopButton, &tripletButton })
        addChildComponent (b);

    undoButton.onClick = [this] { studio.getDocument().undo(); };
    redoButton.onClick = [this] { studio.getDocument().redo(); };
    menuButton.onClick = [this] { setMenuOpen (! menuOpen); };
    duplicateLoopButton.onClick = [this] { duplicateLoopAction(); };
    tripletButton.onClick = [this] { setTriplets (! state().triplets); };

    longPress.onLongPress = [this] (juce::Point<int>)
    {
        if (expanded)
            return;
        if (clip() != nullptr)
            showLoopMenu (*this, true);
        else
            showNewClipMenu(); // EDIT-1: create a clip when the track has none here
    };

    studio.addListener (this);
    startTimerHz (30);
}

ClipEditor::~ClipEditor()
{
    studio.removeListener (this);
    stopTimer();
    grid = nullptr;
    menu = nullptr;
}

ClipEditor::ViewState& ClipEditor::state()
{
    return editorStates()[trackId];
}

//==============================================================================
void ClipEditor::setSlot (int s)
{
    if (s != slot)
    {
        slot = s;
        selection.clear();
        selectedNotes.clear();
        ownClip = clipPtr();

        auto& st = state();
        if (st.lastSlot != slot)
        {
            // Another clip: fit it again on the next layout.
            st.pixelsPerBeat = 0.0;
            st.scrollY = -1.0;
            st.lastSlot = slot;
        }
    }

    // EDIT-11: the editor comes back expanded if it was when the view was left.
    if (! restored)
    {
        restored = true;
        if (state().expanded && clip() != nullptr)
            setExpanded (true);
    }

    if (expanded && clip() == nullptr)
        setExpanded (false);

    grid->refresh();
    menu->refresh();
    updateToolbar();
    repaint();
}

void ClipEditor::setExpanded (bool e)
{
    if (e && clip() == nullptr)
        e = false;
    if (expanded == e)
        return;

    expanded = e;
    state().expanded = e;

    if (! expanded)
    {
        // EDIT-8: closing the editor clears the selection.
        selection.clear();
        selectedNotes.clear();
        menuOpen = false;
        menuAutoOpened = false;
    }

    grid->setVisible (expanded);
    for (auto* b : { &undoButton, &redoButton, &menuButton, &duplicateLoopButton, &tripletButton })
        b->setVisible (expanded);
    menu->setVisible (expanded && menuOpen);

    if (onExpandedChanged != nullptr)
        onExpandedChanged (expanded);

    grid->refresh();
    menu->refresh();
    updateToolbar();
    resized();
    repaint();
}

void ClipEditor::showQuantizeSettings()
{
    if (clip() == nullptr)
        return;
    setExpanded (true);
    setMenuOpen (true, NoteEditMenu::quantizeTab);
}

void ClipEditor::setMenuOpen (bool open, int tab)
{
    menuAutoOpened = false;
    if (tab >= 0)
        state().menuTab = tab;
    menuOpen = open && expanded;
    menu->setVisible (menuOpen);
    menuButton.setHighlight (menuOpen);
    menu->refresh();
    resized();
}

//==============================================================================
const Track* ClipEditor::track() const
{
    return studio.getSet().findTrack (trackId);
}

ClipPtr ClipEditor::clipPtr() const
{
    auto* t = track();
    if (t == nullptr || slot < 0 || slot >= numScenes)
        return nullptr;
    return t->slots[(size_t) slot];
}

const Clip* ClipEditor::clip() const
{
    return clipPtr().get();
}

bool ClipEditor::isDrumClip() const
{
    auto* t = track();
    return t != nullptr && t->isDrum();
}

void ClipEditor::setSelection (Selection s)
{
    auto* c = clip();
    selection.clear();
    selectedNotes.clear();

    if (c != nullptr)
    {
        std::sort (s.begin(), s.end());
        s.erase (std::unique (s.begin(), s.end()), s.end());
        for (auto i : s)
            if (i >= 0 && i < (int) c->notes.size())
            {
                selection.push_back (i);
                selectedNotes.push_back (c->notes[(size_t) i]);
            }
    }

    // EDIT-10: the note-edit menu opens by itself when there is a selection (once the touch
    // that made it ends, so the grid does not resize under the finger), and closes again when
    // the selection goes away if it opened by itself.
    if (! grid->isGestureActive())
    {
        if (! selection.empty() && expanded && ! menuOpen)
        {
            setMenuOpen (true);
            menuAutoOpened = true;
        }
        else if (selection.empty() && menuOpen && menuAutoOpened)
        {
            setMenuOpen (false);
        }
    }

    grid->repaint();
    menu->refresh();
}

Selection ClipEditor::targetNotes() const
{
    if (! selection.empty())
        return selection;
    if (auto* c = clip())
        return notesInLoop (*c);
    return {};
}

void ClipEditor::commit (const ClipEdit& result, bool keepSelection)
{
    if (result.clip == nullptr || slot < 0)
        return;

    ownClip = result.clip;
    const int id = trackId, s = slot;
    auto c = result.clip;
    studio.getDocument().edit ([id, s, c] (Set& set)
    {
        if (auto* t = set.findTrack (id))
            if (t->slots[(size_t) s] != nullptr)
                t->slots[(size_t) s] = c;
    });

    setSelection (keepSelection ? result.selection : Selection {});
}

void ClipEditor::commitClip (ClipPtr c, bool keepSelection)
{
    commit ({ std::move (c), keepSelection ? selection : Selection {} }, keepSelection);
}

void ClipEditor::beginGesture() { studio.getDocument().beginGesture(); }
void ClipEditor::endGesture()   { studio.getDocument().endGesture(); }

//==============================================================================
double ClipEditor::getCellGrid() const
{
    auto& st = const_cast<ClipEditor*> (this)->state();
    const auto& divisions = getGridDivisions();
    if (st.gridChoice > 0 && st.gridChoice <= (int) divisions.size())
    {
        const double beats = divisions[(size_t) st.gridChoice - 1].beats;
        return st.triplets ? tripletOf (beats) : beats;
    }
    return grid->getAdaptiveGrid();
}

double ClipEditor::getSnapGrid() const
{
    return const_cast<ClipEditor*> (this)->state().gridChoice == 0 ? 0.0 : getCellGrid();
}

double ClipEditor::getNudgeStep() const
{
    const double snap = getSnapGrid();
    return snap > 0.0 ? snap : 1.0 / 32.0;
}

void ClipEditor::setGridChoice (int choice)
{
    state().gridChoice = choice;
    grid->repaint();
    menu->refresh();
}

void ClipEditor::setTriplets (bool t)
{
    state().triplets = t;
    tripletButton.setToggleState (t, juce::dontSendNotification);
    grid->repaint();
    menu->refresh();
}

void ClipEditor::audition (int pitch, float velocity)
{
    studio.noteOn (trackId, pitch, juce::jmax (0.3f, velocity));
    const int token = ++auditionTokens[pitch];

    juce::Component::SafePointer<ClipEditor> safe (this);
    juce::Timer::callAfterDelay (180, [safe, pitch, token]
    {
        if (safe != nullptr && safe->auditionTokens[pitch] == token)
            safe->studio.noteOff (safe->trackId, pitch);
    });
}

//==============================================================================
void ClipEditor::zoomToLoop()
{
    if (auto* c = clip())
        grid->zoomToRange (c->loopStart, c->loopLength);
}

void ClipEditor::zoomToClip()
{
    if (auto* c = clip())
        grid->zoomToRange (0.0, c->lengthBeats());
}

void ClipEditor::duplicateLoopAction()
{
    if (auto* c = clip())
    {
        if (c->loopEnd() + c->loopLength > maxClipBars * (double) beatsPerBar + 1.0e-9)
            return; // would pass 16 bars
        commitClip (duplicateLoop (*c), false);
        if (expanded)
            zoomToLoop();
    }
}

void ClipEditor::deleteClipOrLoopContent()
{
    auto* c = clip();
    if (c == nullptr)
        return;

    const bool loopIsClip = c->loopStart < 1.0e-9 && std::abs (c->loopLength - c->lengthBeats()) < 1.0e-9;
    if (loopIsClip)
    {
        const int id = trackId, s = slot;
        studio.getDocument().edit ([id, s] (Set& set) { deleteClip (set, id, s); });
    }
    else
    {
        commitClip (deleteLoopContent (*c), false);
    }
}

void ClipEditor::showLoopMenu (juce::Component& target, bool fromPreview)
{
    auto* c = clip();
    if (c == nullptr)
        return;

    const bool loopIsClip = c->loopStart < 1.0e-9 && std::abs (c->loopLength - c->lengthBeats()) < 1.0e-9;
    const bool canDuplicate = c->loopEnd() + c->loopLength <= maxClipBars * (double) beatsPerBar + 1.0e-9;

    enum { duplicateId = 1, cropId, deleteId, zoomLoopId, zoomClipId };
    juce::PopupMenu m;
    m.addSectionHeader (loopIsClip ? "Clip" : "Loop");

    if (fromPreview)
    {
        // EDIT-1
        m.addItem (duplicateId, "Duplicate Loop", canDuplicate);
        m.addItem (cropId, "Crop to Loop", ! loopIsClip);
        m.addItem (deleteId, loopIsClip ? "Delete Clip" : "Delete Loop Content");
    }
    else if (loopIsClip)
    {
        // EDIT-12, loop = clip
        m.addItem (duplicateId, "Duplicate", canDuplicate);
        m.addItem (deleteId, "Delete Clip");
        m.addItem (zoomClipId, "Zoom to Clip");
    }
    else
    {
        // EDIT-12, loop shorter than the clip
        m.addItem (zoomLoopId, "Zoom to Loop");
        m.addItem (cropId, "Crop to Loop");
        m.addItem (deleteId, "Delete Loop Content");
        m.addItem (duplicateId, "Duplicate Loop", canDuplicate);
        m.addItem (zoomClipId, "Zoom to Clip");
    }

    juce::Component::SafePointer<ClipEditor> safe (this);
    showMenu (m, target, [safe] (int result)
    {
        if (safe == nullptr || safe->clip() == nullptr)
            return;
        switch (result)
        {
            case duplicateId: safe->duplicateLoopAction(); break;
            case cropId:      safe->commitClip (cropToLoop (*safe->clip()), false); if (safe->expanded) safe->zoomToClip(); break;
            case deleteId:    safe->deleteClipOrLoopContent(); break;
            case zoomLoopId:  safe->zoomToLoop(); break;
            case zoomClipId:  safe->zoomToClip(); break;
            default: break;
        }
    });
}

void ClipEditor::showNewClipMenu()
{
    if (slot < 0)
    {
        auto* t = track();
        for (int s = 0; t != nullptr && s < numScenes; ++s)
            if (t->slots[(size_t) s] == nullptr) { slot = s; break; }
        if (slot < 0)
            return;
    }

    // CAP-2: fixed-length clip.
    juce::PopupMenu m;
    m.addSectionHeader ("New clip length");
    for (int bars : { 1, 2, 4, 8, 16 })
        m.addItem (bars, juce::String (bars) + (bars == 1 ? " bar" : " bars"));

    juce::Component::SafePointer<ClipEditor> safe (this);
    showMenu (m, *this, [safe] (int bars)
    {
        if (safe != nullptr && bars > 0)
            safe->studio.createFixedLengthClip (safe->trackId, safe->slot, bars);
    });
}

//==============================================================================
void ClipEditor::studioChanged()
{
    auto c = clipPtr();
    if (expanded && c == nullptr)
        setExpanded (false);

    if (c != ownClip)
    {
        // Changed elsewhere (undo, overdub, quantize button): keep the notes that still exist.
        ownClip = c;
        Selection remapped;
        if (c != nullptr)
            for (auto& n : selectedNotes)
                for (size_t i = 0; i < c->notes.size(); ++i)
                    if (sameNote (c->notes[i], n))
                    {
                        remapped.push_back ((int) i);
                        break;
                    }
        selection.clear();
        selectedNotes.clear();
        if (c != nullptr)
            for (auto i : remapped)
            {
                selection.push_back (i);
                selectedNotes.push_back (c->notes[(size_t) i]);
            }
    }

    grid->refresh();
    menu->refresh();
    updateToolbar();
    repaint();
}

void ClipEditor::captureChanged()
{
    if (expanded)
        grid->repaint();
    else
        repaint();
}

void ClipEditor::timerCallback()
{
    if (expanded)
    {
        grid->updatePlayhead();
        return;
    }

    // Collapsed: repaint only the strips the playhead leaves and enters.
    auto* c = clip();
    const float x = c != nullptr && studio.getPlayingSlot (trackId) == slot ? previewPlayheadX (*c) : -1.0f;
    if (std::abs (x - lastPlayheadX) < 0.5f)
        return;

    const auto area = getLocalBounds();
    for (float px : { lastPlayheadX, x })
        if (px >= 0.0f)
            repaint (juce::Rectangle<int> ((int) px - 3, area.getY(), 6, area.getHeight()));
    lastPlayheadX = x;
}

void ClipEditor::updateToolbar()
{
    auto& doc = studio.getDocument();
    undoButton.setEnabled (doc.canUndo());
    redoButton.setEnabled (doc.canRedo());
    menuButton.setHighlight (menuOpen);
    tripletButton.setToggleState (state().triplets, juce::dontSendNotification);

    auto* c = clip();
    duplicateLoopButton.setEnabled (c != nullptr && c->loopEnd() + c->loopLength <= maxClipBars * (double) beatsPerBar + 1.0e-9);
}

//==============================================================================
juce::Rectangle<int> ClipEditor::cornerArea() const
{
    if (expanded)
        return grid->getGridArea().translated (grid->getX(), grid->getY());
    return previewArea().toNearestInt();
}

void ClipEditor::layoutCornerButton()
{
    const bool show = clip() != nullptr;
    cornerButton->setVisible (show);
    if (! show)
        return;

    const int size = expanded ? 44 : 34;
    const auto area = cornerArea().reduced (expanded ? 6 : 2);
    const int corner = state().corner;
    const int x = (corner & 1) ? area.getRight() - size : area.getX();
    const int y = (corner & 2) ? area.getBottom() - size : area.getY();
    cornerButton->setBounds (x, y, size, size);
    cornerButton->toFront (false);
}

void ClipEditor::resized()
{
    auto area = getLocalBounds();

    if (expanded)
    {
        const bool landscape = area.getWidth() > area.getHeight() * 1.3f;
        const int toolbarSize = 52;

        juce::Rectangle<int> toolbar;
        if (landscape)
            toolbar = area.removeFromLeft (toolbarSize);
        else
            toolbar = area.removeFromBottom (toolbarSize);

        if (menuOpen)
        {
            if (landscape)
                menu->setBounds (area.removeFromRight (juce::jmin (340, area.getWidth() * 45 / 100)));
            else
                menu->setBounds (area.removeFromBottom (juce::jmin (196, area.getHeight() * 45 / 100)));
        }

        grid->setBounds (area);

        auto buttons = { &undoButton, &redoButton, &menuButton, &duplicateLoopButton, &tripletButton };
        const int count = (int) buttons.size();
        int i = 0;
        for (auto* b : buttons)
        {
            if (landscape)
            {
                const int h = juce::jmin (52, toolbar.getHeight() / count);
                b->setBounds (toolbar.getX() + 2, toolbar.getY() + i * h, toolbarSize - 4, h);
            }
            else
            {
                const int w = juce::jmin (76, toolbar.getWidth() / count);
                b->setBounds (toolbar.getX() + i * w, toolbar.getY() + 2, w, toolbarSize - 4);
            }
            ++i;
        }
    }

    layoutCornerButton();
}

//==============================================================================
juce::Rectangle<float> ClipEditor::previewArea() const
{
    return getLocalBounds().toFloat().reduced (4.0f, 2.0f);
}

float ClipEditor::previewPlayheadX (const Clip& c) const
{
    const auto area = previewArea().reduced (10.0f, 8.0f);
    double position = studio.getClipPosition (trackId);
    if (position < c.loopStart || position > c.loopEnd())
        return -1.0f;
    return area.getX() + (float) ((position - c.loopStart) / c.loopLength) * area.getWidth();
}

void ClipEditor::paint (juce::Graphics& g)
{
    const auto& t = Theme::get();

    if (expanded)
    {
        g.fillAll (t.surface);
        g.setColour (t.outline.withAlpha (0.6f));
        const bool landscape = getWidth() > getHeight() * 1.3f;
        if (landscape)
            g.drawVerticalLine (52, 0.0f, (float) getHeight());
        else
            g.drawHorizontalLine (getHeight() - 52, 0.0f, (float) getWidth());
        return;
    }

    auto bounds = previewArea();
    g.setColour (t.surface);
    g.fillRoundedRectangle (bounds, 8.0f);

    auto* tr = track();
    auto* c = clip();
    if (tr == nullptr)
        return;

    if (c == nullptr)
    {
        // CAP-2: big "+" offering fixed-length clips.
        auto plus = bounds.withSizeKeepingCentre (52.0f, 52.0f).translated (-70.0f, 0.0f);
        g.setColour (t.surfaceRaised);
        g.fillEllipse (plus);
        drawIcon (g, Icon::plus, plus.reduced (12.0f), t.text, 2.4f);

        auto text = bounds.withLeft (plus.getRight() + 14.0f);
        g.setColour (t.text);
        g.setFont (Theme::font (15.0f, true));
        g.drawText ("Add clip", text.withTrimmedBottom (text.getHeight() * 0.5f), juce::Justification::bottomLeft);
        g.setColour (t.textDim);
        g.setFont (Theme::font (13.0f));
        g.drawText ("or just play the pads", text.withTrimmedTop (text.getHeight() * 0.5f + 2.0f),
                    juce::Justification::topLeft);
        return;
    }

    paintPreview (g, *tr, *c);
}

void ClipEditor::paintPreview (juce::Graphics& g, const Track& tr, const Clip& c)
{
    const auto& t = Theme::get();
    const auto& set = studio.getSet();
    const auto colour = Theme::trackColour (tr.colour);
    auto area = previewArea().reduced (10.0f, 8.0f);

    const auto pending = studio.getPendingNotes (trackId, slot);
    const auto rows = previewRows (c.notes, pending, c.loopStart, c.loopEnd(), tr.isDrum(), tr.perf.selectedPad,
                                   set.inKey, set.root, set.scale);

    // EDIT-2 inline: only rows with notes; rows never get taller than 16 px.
    const float rowH = juce::jmin (16.0f, area.getHeight() / (float) juce::jmax (1, (int) rows.size()));
    auto rowsArea = area.withSizeKeepingCentre (area.getWidth(), rowH * (float) rows.size());
    const double loopLength = juce::jmax (0.25, c.loopLength);
    auto xOf = [&] (double beat) { return area.getX() + (float) ((beat - c.loopStart) / loopLength) * area.getWidth(); };

    // Beat and bar lines.
    for (double b = std::ceil (c.loopStart); b <= c.loopEnd() + 1.0e-6; b += 1.0)
    {
        const bool bar = std::fmod (b, (double) beatsPerBar) < 1.0e-6;
        g.setColour (t.outline.withAlpha (bar ? 0.7f : 0.25f));
        g.drawVerticalLine ((int) xOf (b), area.getY(), area.getBottom());
    }

    auto drawNotes = [&] (const std::vector<Note>& notes, bool ghost)
    {
        for (auto& n : notes)
        {
            if (n.start < c.loopStart || n.start >= c.loopEnd())
                continue;
            const int row = rowIndexOf (rows, n.pitch);
            if (row < 0)
                continue;
            const float x = xOf (n.start);
            const float w = juce::jmax (2.5f, (float) (n.length / loopLength) * area.getWidth() - 1.0f);
            const float y = rowsArea.getBottom() - (float) (row + 1) * rowH;
            juce::Rectangle<float> r (x, y + 1.0f, juce::jmin (w, area.getRight() - x), juce::jmax (2.0f, rowH - 2.0f));
            if (ghost)
            {
                g.setColour (t.text.withAlpha (0.12f));
                g.fillRoundedRectangle (r, 1.5f);
                g.setColour (t.text.withAlpha (0.75f));
                g.drawRoundedRectangle (r, 1.5f, 1.0f);
            }
            else
            {
                g.setColour (colour.withMultipliedSaturation (0.2f + 0.8f * n.velocity)
                                   .withMultipliedBrightness (0.6f + 0.4f * n.velocity));
                g.fillRoundedRectangle (r, 1.5f);
            }
        }
    };

    drawNotes (c.notes, false);
    drawNotes (pending, true);

    // Automation (DEV-30/31): committed lanes solid, pending recording dotted.
    auto drawLane = [&] (const ParamTarget& target, const std::vector<AutomationPoint>& points, bool pendingLane)
    {
        auto* spec = specForTarget (tr, target);
        if (spec == nullptr || points.empty() || target.paramIndex < 0 || target.paramIndex >= (int) spec->params.size())
            return;
        const auto& param = spec->params[(size_t) target.paramIndex];
        auto yOf = [&] (float value) { return area.getBottom() - param.toNormalised (value) * area.getHeight(); };

        juce::Path path;
        if (pendingLane)
        {
            bool started = false;
            for (auto& p : points)
            {
                if (p.beat < c.loopStart - 1.0e-6 || p.beat > c.loopEnd() + 1.0e-6)
                    continue;
                if (! started) path.startNewSubPath (xOf (p.beat), yOf (p.value));
                else           path.lineTo (xOf (p.beat), yOf (p.value));
                started = true;
            }
        }
        else
        {
            AutomationLane lane { target, points };
            path.startNewSubPath (xOf (c.loopStart), yOf (lane.valueAt (c.loopStart)));
            for (auto& p : points)
                if (p.beat > c.loopStart && p.beat < c.loopEnd())
                    path.lineTo (xOf (p.beat), yOf (p.value));
            path.lineTo (xOf (c.loopEnd()), yOf (lane.valueAt (c.loopEnd())));
        }

        if (pendingLane)
        {
            juce::Path dashed;
            const float dashes[] = { 2.0f, 3.0f };
            juce::PathStrokeType (1.6f).createDashedStroke (dashed, path, dashes, 2);
            g.setColour (t.text.withAlpha (0.85f));
            g.fillPath (dashed);
        }
        else
        {
            g.setColour (colour.brighter (0.4f).withAlpha (0.85f));
            g.strokePath (path, juce::PathStrokeType (1.6f));
        }
    };

    for (auto& lane : c.automation)
        drawLane (lane.target, lane.points, false);

    const bool playingHere = studio.getPlayingSlot (trackId) == slot;
    if (playingHere)
        for (auto& lane : studio.getPendingAutomation (trackId))
            drawLane (lane.target, lane.points, true);

    if (c.notes.empty() && pending.empty())
    {
        const double bars = c.loopLength / beatsPerBar;
        const juce::String length = std::abs (bars - std::round (bars)) < 1.0e-6
                                        ? juce::String ((int) std::round (bars)) + (bars < 1.5 ? " bar" : " bars")
                                        : juce::String (c.loopLength, 2) + " beats";
        g.setColour (t.textDim);
        g.setFont (Theme::font (13.0f));
        g.drawText ("Empty clip, " + length + " - play the pads to add notes", area, juce::Justification::centred);
    }

    if (playingHere)
    {
        const float x = previewPlayheadX (c);
        if (x >= 0.0f)
        {
            g.setColour (t.text);
            g.fillRect (x - 0.75f, previewArea().getY() + 2.0f, 1.5f, previewArea().getHeight() - 4.0f);
        }
    }
}

//==============================================================================
void ClipEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! expanded)
        longPress.mouseDown (e);
}

void ClipEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! expanded)
        longPress.mouseDrag (e);
}

void ClipEditor::mouseUp (const juce::MouseEvent& e)
{
    if (expanded)
        return;
    if (longPress.mouseUp() || e.getDistanceFromDragStart() > 12)
        return;

    if (clip() == nullptr)
        showNewClipMenu();
    else
        setExpanded (true);
}

} // namespace rb
