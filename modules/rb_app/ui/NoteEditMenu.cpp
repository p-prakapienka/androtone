namespace rb
{

//==============================================================================
// One vertical slider per target note (Velocity / Pitches tabs). One finger draws across the
// sliders; a second finger turns it into a linear ramp between the two touches (EDIT-10).
class NoteEditMenu::ValueBars : public juce::Component
{
public:
    ValueBars (NoteEditMenu& m, bool pitch) : menu (m), pitchMode (pitch) {}

    static constexpr float pitchRange = 12.0f;

    float valueOf (const Note& n) const { return pitchMode ? n.pitchOffset : n.velocity; }

    float valueAtY (float y) const
    {
        const auto area = barArea();
        const float norm = juce::jlimit (0.0f, 1.0f, 1.0f - (y - area.getY()) / juce::jmax (1.0f, area.getHeight()));
        if (pitchMode)
            return (float) juce::roundToInt ((norm * 2.0f - 1.0f) * pitchRange);
        return juce::jlimit (0.01f, 1.0f, norm);
    }

    juce::Rectangle<float> barArea() const { return getLocalBounds().toFloat().reduced (2.0f, 4.0f); }

    int columnAt (float x, int count) const
    {
        const auto area = barArea();
        return juce::jlimit (0, juce::jmax (0, count - 1), (int) ((x - area.getX()) / area.getWidth() * (float) count));
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = Theme::get();
        auto& owner = menu.owner;
        auto* c = owner.clip();
        auto* tr = owner.track();
        const auto area = barArea();

        g.setColour (t.background);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

        if (c == nullptr || tr == nullptr)
            return;

        const auto columns = owner.targetNotes();
        if (columns.empty())
        {
            g.setColour (t.textDim);
            g.setFont (Theme::font (13.0f));
            g.drawText ("No notes in the loop", area, juce::Justification::centred);
            return;
        }

        const auto colour = Theme::trackColour (tr->colour);
        const float w = area.getWidth() / (float) columns.size();
        const float gap = w > 6.0f ? juce::jmin (4.0f, w * 0.2f) : 0.0f;

        if (pitchMode)
        {
            g.setColour (t.outline);
            g.fillRect (area.getX(), area.getCentreY() - 0.5f, area.getWidth(), 1.0f);
        }

        for (size_t k = 0; k < columns.size(); ++k)
        {
            const int i = columns[k];
            if (i < 0 || i >= (int) c->notes.size())
                continue;
            const auto& n = c->notes[(size_t) i];
            const float x = area.getX() + w * (float) k + gap * 0.5f;
            auto column = juce::Rectangle<float> (x, area.getY(), juce::jmax (1.0f, w - gap), area.getHeight());

            g.setColour (t.surfaceRaised.withAlpha (0.6f));
            g.fillRoundedRectangle (column, juce::jmin (3.0f, column.getWidth() * 0.3f));

            juce::Rectangle<float> bar;
            if (pitchMode)
            {
                const float v = juce::jlimit (-pitchRange, pitchRange, n.pitchOffset) / pitchRange;
                const float y = column.getCentreY() - v * column.getHeight() * 0.5f;
                bar = juce::Rectangle<float>::leftTopRightBottom (column.getX(), juce::jmin (y, column.getCentreY()),
                                                                  column.getRight(), juce::jmax (y, column.getCentreY()));
                if (bar.getHeight() < 2.0f)
                    bar = bar.withSizeKeepingCentre (bar.getWidth(), 2.0f);
                g.setColour (colour);
            }
            else
            {
                bar = column.withTrimmedTop (column.getHeight() * (1.0f - n.velocity));
                g.setColour (colour.withMultipliedSaturation (0.2f + 0.8f * n.velocity));
            }
            g.fillRoundedRectangle (bar, juce::jmin (3.0f, column.getWidth() * 0.3f));

            if (w >= 26.0f)
            {
                g.setColour (t.text);
                g.setFont (Theme::font (10.0f));
                const auto text = pitchMode ? ((n.pitchOffset > 0 ? "+" : "") + juce::String (juce::roundToInt (n.pitchOffset)))
                                            : juce::String (juce::roundToInt (n.velocity * 127.0f));
                g.drawText (text, column.removeFromTop (14.0f), juce::Justification::centred);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto& owner = menu.owner;
        touches[e.source.getIndex()] = e.position;

        if (touches.size() == 1)
        {
            baseClip = owner.clipPtr();
            targets = owner.targetNotes();
            hadSelection = ! owner.getSelection().empty();
            if (baseClip == nullptr || targets.empty())
            {
                baseClip = nullptr;
                return;
            }
            values.clear();
            for (auto i : targets)
                values.push_back (valueOf (baseClip->notes[(size_t) i]));
            owner.beginGesture();
            gestureOpen = true;
            lastColumn = columnAt (e.position.x, (int) targets.size());
            lastValue = valueAtY (e.position.y);
        }
        apply (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        touches[e.source.getIndex()] = e.position;
        apply (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        touches.erase (e.source.getIndex());
        if (touches.empty() && gestureOpen)
        {
            gestureOpen = false;
            baseClip = nullptr;
            menu.owner.endGesture();
        }
        else if (touches.size() == 1)
        {
            // Back to drawing with the remaining finger.
            lastColumn = columnAt (touches.begin()->second.x, (int) targets.size());
            lastValue = valueAtY (touches.begin()->second.y);
        }
    }

private:
    void apply (const juce::MouseEvent& e)
    {
        if (baseClip == nullptr || targets.empty())
            return;

        const int count = (int) targets.size();
        if (touches.size() >= 2)
        {
            // Two fingers: ramp from the leftmost to the rightmost touch.
            auto it = touches.begin();
            auto a = it->second;
            auto b = (++it)->second;
            if (b.x < a.x)
                std::swap (a, b);
            values = velocityRamp ((size_t) count, valueAtY (a.y), valueAtY (b.y));
            if (pitchMode)
                for (auto& v : values)
                    v = (float) juce::roundToInt (v);
        }
        else
        {
            const int column = columnAt (e.position.x, count);
            const float value = valueAtY (e.position.y);
            drawValues (values, lastColumn, lastValue, column, value);
            lastColumn = column;
            lastValue = value;
        }

        auto& owner = menu.owner;
        auto result = pitchMode ? setPitchOffsets (*baseClip, targets, values) : setVelocities (*baseClip, targets, values);
        owner.commit (result, hadSelection);
    }

    NoteEditMenu& menu;
    const bool pitchMode;
    std::map<int, juce::Point<float>> touches;
    ClipPtr baseClip;
    Selection targets;
    std::vector<float> values;
    bool hadSelection = false, gestureOpen = false;
    int lastColumn = 0;
    float lastValue = 0.0f;
};

//==============================================================================
NoteEditMenu::NoteEditMenu (ClipEditor& o) : owner (o)
{
    const char* tabNames[] = { "Operations", "Transpose", "Quantize", "Velocity", "Pitches" };
    for (int i = 0; i < numTabs; ++i)
    {
        auto b = std::make_unique<PillButton> (tabNames[i], PillButton::Style::subtle);
        b->onClick = [this, i] { showTab (i); };
        addAndMakeVisible (*b);
        tabButtons.push_back (std::move (b));
    }

    // Operations: grid value.
    auto makeDivisionButtons = [this] (std::vector<std::unique_ptr<PillButton>>& list)
    {
        list.push_back (std::make_unique<PillButton> ("Off"));
        for (auto& d : getGridDivisions())
            list.push_back (std::make_unique<PillButton> (d.label));
        list.push_back (std::make_unique<PillButton> ("T"));
        for (auto& b : list)
            addChildComponent (*b);
    };
    makeDivisionButtons (gridButtons);
    makeDivisionButtons (quantizeButtons);
    const int numDivisions = (int) getGridDivisions().size();

    for (int i = 0; i < (int) gridButtons.size(); ++i)
        gridButtons[(size_t) i]->onClick = [this, i, numDivisions]
        {
            if (i == 0)
                owner.setGridChoice (owner.state().gridChoice == 0 ? -1 : 0);
            else if (i <= numDivisions)
                owner.setGridChoice (owner.state().gridChoice == i ? -1 : i); // tap again: follow zoom
            else
                owner.setTriplets (! owner.state().triplets);
        };

    for (auto* b : { &shortenButton, &nudgeLeftButton, &deleteButton, &nudgeRightButton, &extendButton })
        addChildComponent (b);

    shortenButton.onClick = [this]
    {
        const double step = owner.getNudgeStep();
        applyToTargets ([step] (const Clip& c, const Selection& s) { return resizeNotes (c, s, -step, juce::jmin (step, 1.0 / 32.0)); });
    };
    extendButton.onClick = [this]
    {
        const double step = owner.getNudgeStep();
        applyToTargets ([step] (const Clip& c, const Selection& s) { return resizeNotes (c, s, step, 1.0 / 32.0); });
    };
    nudgeLeftButton.onClick = [this]
    {
        const double step = owner.getNudgeStep();
        applyToTargets ([step] (const Clip& c, const Selection& s) { return moveNotes (c, s, -step, 0); });
    };
    nudgeRightButton.onClick = [this]
    {
        const double step = owner.getNudgeStep();
        applyToTargets ([step] (const Clip& c, const Selection& s) { return moveNotes (c, s, step, 0); });
    };
    deleteButton.onClick = [this]
    {
        auto* c = owner.clip();
        if (c == nullptr || owner.getSelection().empty())
            return;
        owner.commit (deleteNotes (*c, owner.getSelection()), false);
        owner.setMenuOpen (false); // deleting closes the menu
    };

    // Transpose.
    for (auto* b : { &stepDownButton, &stepUpButton, &octaveDownButton, &octaveUpButton })
        addChildComponent (b);

    auto transpose = [this] (int steps)
    {
        const auto& set = owner.getStudio().getSet();
        const bool drum = owner.isDrumClip();
        const bool inKey = set.inKey;
        const int root = set.root, scale = set.scale;
        applyToTargets ([=] (const Clip& c, const Selection& s) { return transposeNotes (c, s, steps, drum, inKey, root, scale); });
        auditionFirstTarget();
    };
    stepDownButton.onClick = [transpose] { transpose (-1); };
    stepUpButton.onClick = [transpose] { transpose (1); };
    octaveDownButton.onClick = [this]
    {
        applyToTargets ([] (const Clip& c, const Selection& s) { return transposeOctave (c, s, -1); });
        auditionFirstTarget();
    };
    octaveUpButton.onClick = [this]
    {
        applyToTargets ([] (const Clip& c, const Selection& s) { return transposeOctave (c, s, 1); });
        auditionFirstTarget();
    };

    // Quantization (shares its settings with the action bar's quantize button, INS-5).
    for (int i = 0; i < (int) quantizeButtons.size(); ++i)
        quantizeButtons[(size_t) i]->onClick = [this, i, numDivisions]
        {
            const auto& divisions = getGridDivisions();
            if (i == 0)
            {
                quantizeOff = true;
                applyToTargets ([] (const Clip& c, const Selection& s) { return quantizeNotes (c, s, 0.0, 1.0f); });
            }
            else if (i <= numDivisions)
            {
                quantizeOff = false;
                InstrumentView::quantizeGrid = divisions[(size_t) i - 1].beats;
                applyQuantize();
            }
            else
            {
                InstrumentView::quantizeTriplets = ! InstrumentView::quantizeTriplets;
                if (! quantizeOff)
                    applyQuantize();
            }
            refresh();
        };

    addChildComponent (amountSlider);
    amountSlider.setRange (0.0, 100.0, 1.0);
    amountSlider.setValue (InstrumentView::quantizeStrength * 100.0, juce::dontSendNotification);
    amountSlider.onDragStart = [this] { amountDragging = true; owner.beginGesture(); };
    amountSlider.onDragEnd = [this] { amountDragging = false; owner.endGesture(); };
    amountSlider.onValueChange = [this]
    {
        InstrumentView::quantizeStrength = (float) amountSlider.getValue() / 100.0f;
        if (! quantizeOff)
            applyQuantize();
        repaint (amountLabelArea);
    };

    velocityBars = std::make_unique<ValueBars> (*this, false);
    pitchBars = std::make_unique<ValueBars> (*this, true);
    addChildComponent (*velocityBars);
    addChildComponent (*pitchBars);

    showTab (owner.state().menuTab);
}

NoteEditMenu::~NoteEditMenu() = default;

int NoteEditMenu::currentTab() const
{
    int tab = juce::jlimit (0, numTabs - 1, owner.state().menuTab);
    if (tab == pitchesTab && ! owner.isDrumClip())
        tab = operationsTab;
    return tab;
}

std::vector<juce::Component*> NoteEditMenu::componentsOf (int tab)
{
    std::vector<juce::Component*> list;
    switch (tab)
    {
        case operationsTab:
            for (auto& b : gridButtons) list.push_back (b.get());
            for (auto* b : { &shortenButton, &nudgeLeftButton, &deleteButton, &nudgeRightButton, &extendButton })
                list.push_back (b);
            break;
        case transposeTab:
            for (auto* b : { &stepDownButton, &stepUpButton, &octaveDownButton, &octaveUpButton })
                list.push_back (b);
            break;
        case quantizeTab:
            for (auto& b : quantizeButtons) list.push_back (b.get());
            list.push_back (&amountSlider);
            break;
        case velocityTab: list.push_back (velocityBars.get()); break;
        case pitchesTab:  list.push_back (pitchBars.get()); break;
        default: break;
    }
    return list;
}

void NoteEditMenu::showTab (int tab)
{
    owner.state().menuTab = tab;
    const int shown = currentTab();
    for (int i = 0; i < numTabs; ++i)
        for (auto* c : componentsOf (i))
            c->setVisible (i == shown);
    refresh();
    resized();
}

void NoteEditMenu::refresh()
{
    const int tab = currentTab();
    const bool drum = owner.isDrumClip();
    const auto& st = owner.state();

    for (int i = 0; i < numTabs; ++i)
    {
        tabButtons[(size_t) i]->setToggleState (i == tab, juce::dontSendNotification);
        tabButtons[(size_t) i]->setVisible (i != pitchesTab || drum);
    }
    for (int i = 0; i < numTabs; ++i)
        for (auto* c : componentsOf (i))
            c->setVisible (i == tab);

    // Operations
    const int numDivisions = (int) getGridDivisions().size();
    for (int i = 0; i < (int) gridButtons.size(); ++i)
        gridButtons[(size_t) i]->setToggleState (i <= numDivisions ? st.gridChoice == i : st.triplets,
                                                 juce::dontSendNotification);

    const bool hasClip = owner.clip() != nullptr;
    const bool hasTargets = ! owner.targetNotes().empty();
    for (auto* b : { &shortenButton, &nudgeLeftButton, &nudgeRightButton, &extendButton, &stepDownButton, &stepUpButton })
        b->setEnabled (hasClip && hasTargets);
    deleteButton.setEnabled (! owner.getSelection().empty());
    octaveDownButton.setEnabled (hasTargets && ! drum); // EDIT-10: no octaves for drums
    octaveUpButton.setEnabled (hasTargets && ! drum);
    stepDownButton.setLabel (drum ? "Pad down" : "Step -1");
    stepUpButton.setLabel (drum ? "Pad up" : "Step +1");

    // Quantization
    for (int i = 0; i < (int) quantizeButtons.size(); ++i)
    {
        bool on = false;
        if (i == 0)
            on = quantizeOff;
        else if (i <= numDivisions)
            on = ! quantizeOff && std::abs (getGridDivisions()[(size_t) i - 1].beats - InstrumentView::quantizeGrid) < 1.0e-9;
        else
            on = InstrumentView::quantizeTriplets;
        quantizeButtons[(size_t) i]->setToggleState (on, juce::dontSendNotification);
        quantizeButtons[(size_t) i]->setEnabled (hasTargets);
    }
    if (! amountDragging)
        amountSlider.setValue (InstrumentView::quantizeStrength * 100.0, juce::dontSendNotification);

    velocityBars->repaint();
    pitchBars->repaint();
    repaint();
}

void NoteEditMenu::applyToTargets (const std::function<ClipEdit (const Clip&, const Selection&)>& op)
{
    auto* c = owner.clip();
    if (c == nullptr)
        return;
    const auto targets = owner.targetNotes();
    if (targets.empty())
        return;
    owner.commit (op (*c, targets), ! owner.getSelection().empty());
}

void NoteEditMenu::applyQuantize()
{
    const double grid = InstrumentView::quantizeTriplets ? tripletOf (InstrumentView::quantizeGrid) : InstrumentView::quantizeGrid;
    const float strength = InstrumentView::quantizeStrength;
    applyToTargets ([grid, strength] (const Clip& c, const Selection& s) { return quantizeNotes (c, s, grid, strength); });
}

void NoteEditMenu::auditionFirstTarget()
{
    auto* c = owner.clip();
    const auto targets = owner.targetNotes();
    if (c != nullptr && ! targets.empty() && ! owner.getSelection().empty())
        owner.audition (c->notes[(size_t) targets.front()].pitch, c->notes[(size_t) targets.front()].velocity);
}

//==============================================================================
void NoteEditMenu::paint (juce::Graphics& g)
{
    const auto& t = Theme::get();
    g.fillAll (t.surface);
    g.setColour (t.outline.withAlpha (0.7f));
    g.fillRect (0, 0, getWidth(), 1);
    g.fillRect (0, 0, 1, getHeight());

    const auto& selection = owner.getSelection();
    const auto targets = owner.targetNotes();
    juce::String caption;
    const int tab = currentTab();

    if (selection.empty())
        caption = targets.empty() ? "No notes in the loop" : "No selection: all " + juce::String ((int) targets.size()) + " notes in the loop";
    else
        caption = juce::String ((int) selection.size()) + (selection.size() == 1 ? " note selected" : " notes selected");

    const auto& set = owner.getStudio().getSet();
    if (tab == operationsTab)
    {
        const auto& st = owner.state();
        if (st.gridChoice < 0)
        {
            juce::String label;
            const double cell = owner.getCellGrid();
            for (auto& d : getGridDivisions())
                if (std::abs (d.beats - cell) < 1.0e-6 || std::abs (tripletOf (d.beats) - cell) < 1.0e-6)
                    label = juce::String (d.label) + (std::abs (d.beats - cell) < 1.0e-6 ? "" : "T");
            caption << "  |  Grid follows zoom (" << label << ")";
        }
    }
    else if (tab == transposeTab)
        caption << "  |  " << (owner.isDrumClip() ? "moves hits between pads" : (set.inKey ? "scale steps" : "semitones"));
    else if (tab == velocityTab || tab == pitchesTab)
        caption << "  |  swipe to draw, two fingers to ramp";

    g.setColour (t.textDim);
    g.setFont (Theme::font (11.5f));
    g.drawFittedText (caption, captionArea, juce::Justification::centredLeft, 1, 0.85f);

    if (tab == quantizeTab)
    {
        g.setColour (t.text);
        g.setFont (Theme::font (13.0f));
        g.drawText ("Amount " + juce::String (juce::roundToInt (InstrumentView::quantizeStrength * 100.0f)) + "%",
                    amountLabelArea, juce::Justification::centredLeft);
    }
}

void NoteEditMenu::resized()
{
    auto area = getLocalBounds().reduced (6, 4);

    // Tabs (Pitches only for drums).
    auto tabRow = area.removeFromTop (34);
    int visibleTabs = 0;
    for (auto& b : tabButtons)
        visibleTabs += b->isVisible() ? 1 : 0;
    const int tabW = tabRow.getWidth() / juce::jmax (1, visibleTabs);
    for (auto& b : tabButtons)
        if (b->isVisible())
            b->setBounds (tabRow.removeFromLeft (tabW).reduced (1, 1));

    captionArea = area.removeFromTop (20).reduced (4, 0);
    area.removeFromTop (2);

    auto layoutRow = [] (juce::Rectangle<int> row, const std::vector<juce::Component*>& items, int gap)
    {
        const int w = row.getWidth() / juce::jmax (1, (int) items.size());
        for (auto* c : items)
            c->setBounds (row.removeFromLeft (w).reduced (gap / 2, 0));
    };

    const int rowH = juce::jmin (36, area.getHeight() / 3);

    // Operations
    {
        auto a = area;
        std::vector<juce::Component*> divisions;
        for (auto& b : gridButtons) divisions.push_back (b.get());
        layoutRow (a.removeFromTop (rowH), divisions, 4);
        a.removeFromTop (4);
        layoutRow (a.removeFromTop (juce::jmin (60, a.getHeight())),
                   { &shortenButton, &nudgeLeftButton, &deleteButton, &nudgeRightButton, &extendButton }, 2);
    }

    // Transpose
    {
        auto a = area.withHeight (juce::jmin (64, area.getHeight()));
        layoutRow (a, { &stepDownButton, &stepUpButton, &octaveDownButton, &octaveUpButton }, 6);
    }

    // Quantization
    {
        auto a = area;
        std::vector<juce::Component*> divisions;
        for (auto& b : quantizeButtons) divisions.push_back (b.get());
        layoutRow (a.removeFromTop (rowH), divisions, 4);
        a.removeFromTop (6);
        auto amountRow = a.removeFromTop (juce::jmin (40, a.getHeight()));
        amountLabelArea = amountRow.removeFromLeft (110).reduced (4, 0);
        amountSlider.setBounds (amountRow.reduced (4, 0));
    }

    velocityBars->setBounds (area);
    pitchBars->setBounds (area);
}

} // namespace rb
