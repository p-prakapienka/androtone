namespace rb
{

namespace
{
    constexpr int browserSearchHeight = 44;
    constexpr int browserChipHeight = 38;
    constexpr int browserItemHeight = 56;
    constexpr int browserHeaderHeight = 32;
    constexpr int browserHeartWidth = 48;

    Icon browserIconFor (InstrumentType type)
    {
        switch (type)
        {
            case InstrumentType::drumKit: return Icon::drum;
            case InstrumentType::wave:    return Icon::wave;
            case InstrumentType::sampler: return Icon::sampler;
            case InstrumentType::analog:
            default:                      return Icon::synth;
        }
    }

    juce::String browserTrackFavoriteKey (const juce::String& name) { return "track:" + name; }

    juce::String browserEffectFavoriteKey (EffectType type, const juce::String& name)
    {
        return "fx:" + toTypeId (type) + ":" + name;
    }
}


//==============================================================================
// Horizontally scrolling row of chips (categories, effect types, sample sources).
class BrowserSheet::ChipBar : public juce::Component
{
public:
    std::function<void (int)> onSelect;

    void setChips (const juce::StringArray& labels, int selectedIndex)
    {
        chips = labels;
        selected = selectedIndex;
        layout();
        repaint();
    }

    void setSelected (int index)
    {
        selected = index;
        // Keep the selected chip in view.
        if (juce::isPositiveAndBelow (index, (int) areas.size()))
        {
            auto a = areas[(size_t) index];
            if ((float) a.getX() - offset < 0.0f) offset = (float) a.getX() - 32.0f;
            else if (a.getRight() - offset > (float) getWidth()) offset = (float) a.getRight() - (float) getWidth() + 32.0f;
            clampOffset();
        }
        repaint();
    }

    void resized() override
    {
        layout();
        setSelected (selected);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = Theme::get();
        g.setFont (Theme::font (14.5f, true));
        for (size_t i = 0; i < areas.size(); ++i)
        {
            auto a = areas[i].toFloat().translated (-offset, 0.0f).reduced (0.0f, 3.0f);
            if (a.getRight() < 0.0f || a.getX() > (float) getWidth())
                continue;
            const bool on = (int) i == selected;
            g.setColour (on ? t.accent : (pressed == (int) i ? t.outline : t.surfaceRaised));
            g.fillRoundedRectangle (a, a.getHeight() * 0.5f);
            g.setColour (on ? juce::Colours::black : t.text);
            g.drawText (chips[(int) i], a, juce::Justification::centred);
        }

        // Fade hints when more chips are off screen.
        if (offset > 0.5f)
        {
            g.setGradientFill (juce::ColourGradient (t.surface, 0.0f, 0.0f, t.surface.withAlpha (0.0f), 24.0f, 0.0f, false));
            g.fillRect (0, 0, 24, getHeight());
        }
        if (offset < maxOffset() - 0.5f)
        {
            const float w = (float) getWidth();
            g.setGradientFill (juce::ColourGradient (t.surface.withAlpha (0.0f), w - 24.0f, 0.0f, t.surface, w, 0.0f, false));
            g.fillRect (getWidth() - 24, 0, 24, getHeight());
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStartOffset = offset;
        dragged = false;
        pressed = chipAt (e.position);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragged && std::abs (e.getDistanceFromDragStartX()) > 8)
        {
            dragged = true;
            pressed = -1;
        }
        if (dragged)
        {
            offset = dragStartOffset - (float) e.getDistanceFromDragStartX();
            clampOffset();
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        pressed = -1;
        repaint();
        if (dragged)
            return;
        const int index = chipAt (e.position);
        if (index >= 0 && onSelect != nullptr)
            onSelect (index);
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        offset -= (std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY) * 200.0f;
        clampOffset();
        repaint();
    }

private:
    void layout()
    {
        areas.clear();
        int x = 0;
        const auto font = Theme::font (14.5f, true);
        for (auto& c : chips)
        {
            const int w = juce::jmax (56, (int) juce::GlyphArrangement::getStringWidth (font, c) + 30);
            areas.push_back ({ x, 0, w, getHeight() });
            x += w + 6;
        }
        contentWidth = juce::jmax (0, x - 6);
        clampOffset();
    }

    float maxOffset() const { return (float) juce::jmax (0, contentWidth - getWidth()); }
    void clampOffset() { offset = juce::jlimit (0.0f, maxOffset(), offset); }

    int chipAt (juce::Point<float> p) const
    {
        for (size_t i = 0; i < areas.size(); ++i)
            if (areas[i].toFloat().translated (-offset, 0.0f).contains (p))
                return (int) i;
        return -1;
    }

    juce::StringArray chips;
    std::vector<juce::Rectangle<int>> areas;
    int selected = 0, pressed = -1, contentWidth = 0;
    float offset = 0.0f, dragStartOffset = 0.0f;
    bool dragged = false;
};

//==============================================================================
// The scrolling list of presets / samples with section headers and favorite hearts.
class BrowserSheet::ItemList : public juce::Component
{
public:
    explicit ItemList (BrowserSheet& o) : owner (o), content (*this)
    {
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (4);
        addAndMakeVisible (viewport);
    }

    void refresh()
    {
        tops.clear();
        int y = 0;
        for (auto& item : owner.items)
        {
            if (item.kind == Item::Kind::header && y > 0)
                y += 6;
            tops.push_back (y);
            y += item.kind == Item::Kind::header ? browserHeaderHeight : browserItemHeight;
        }
        contentHeight = y + 8;
        content.setSize (viewport.getMaximumVisibleWidth(), contentHeight);
        content.repaint();
        repaint();
    }

    void scrollToTop() { viewport.setViewPosition (0, 0); }

    // Scrolls so the item with this key is visible; returns false if there is none.
    bool scrollToKey (const juce::String& key)
    {
        for (size_t i = 0; i < owner.items.size(); ++i)
            if (owner.items[i].kind != Item::Kind::header && owner.items[i].key == key)
            {
                const int top = tops[i];
                const int visible = viewport.getMaximumVisibleHeight();
                if (top < viewport.getViewPositionY() || top + browserItemHeight > viewport.getViewPositionY() + visible)
                    viewport.setViewPosition (0, juce::jmax (0, top - visible / 3));
                return true;
            }
        return false;
    }

    void resized() override
    {
        viewport.setBounds (getLocalBounds());
        refresh();
    }

    void paint (juce::Graphics& g) override
    {
        if (! owner.items.empty())
            return;
        const auto& t = Theme::get();
        g.setColour (t.textDim);
        g.setFont (Theme::font (15.0f));
        g.drawFittedText (owner.emptyMessage(), getLocalBounds().reduced (24, 0).withTrimmedBottom (getHeight() / 3),
                          juce::Justification::centred, 4);
    }

private:
    struct Content : public juce::Component
    {
        explicit Content (ItemList& l) : list (l)
        {
            longPress.onLongPress = [this] (juce::Point<int> p)
            {
                const int row = rowAt (p.y);
                pressedRow = -1;
                repaint();
                if (row >= 0)
                    list.owner.showItemMenu (list.owner.items[(size_t) row], *this);
            };
        }

        int rowAt (int y) const
        {
            auto& items = list.owner.items;
            for (size_t i = 0; i < items.size(); ++i)
            {
                const int h = items[i].kind == Item::Kind::header ? browserHeaderHeight : browserItemHeight;
                if (y >= list.tops[i] && y < list.tops[i] + h)
                    return items[i].kind == Item::Kind::header ? -1 : (int) i;
            }
            return -1;
        }

        void paint (juce::Graphics& g) override
        {
            const auto& t = Theme::get();
            auto& items = list.owner.items;
            const auto loaded = list.owner.loadedKey();
            const auto clip = g.getClipBounds();
            auto* tr = list.owner.track();
            const auto colour = Theme::trackColour (tr != nullptr ? tr->colour : 0);
            auto& settings = list.owner.studio.getSettings();

            for (size_t i = 0; i < items.size(); ++i)
            {
                auto& item = items[i];
                const int h = item.kind == Item::Kind::header ? browserHeaderHeight : browserItemHeight;
                juce::Rectangle<int> row (0, list.tops[i], getWidth(), h);
                if (! row.intersects (clip))
                    continue;

                if (item.kind == Item::Kind::header)
                {
                    g.setColour (t.textDim);
                    g.setFont (Theme::font (12.5f, true));
                    g.drawText (item.label.toUpperCase(), row.reduced (4, 0).withTrimmedTop (8), juce::Justification::centredLeft);
                    continue;
                }

                const bool isLoaded = item.key.isNotEmpty() && item.key == loaded;
                auto bg = row.toFloat().reduced (0.0f, 2.0f);
                if (isLoaded || pressedRow == (int) i)
                {
                    g.setColour (pressedRow == (int) i ? t.outline : t.surfaceRaised);
                    g.fillRoundedRectangle (bg, Theme::corner);
                }
                if (isLoaded)
                {
                    g.setColour (colour);
                    g.fillRoundedRectangle (bg.withWidth (4.0f).reduced (0.0f, 10.0f), 2.0f);
                }

                auto inner = row.reduced (10, 0);
                auto iconBox = inner.removeFromLeft (38).withSizeKeepingCentre (38, 38).toFloat();
                g.setColour (isLoaded ? colour.withAlpha (0.25f) : t.surfaceRaised);
                g.fillRoundedRectangle (iconBox, Theme::corner);
                drawIcon (g, item.icon, iconBox.reduced (8.0f), isLoaded ? colour : t.text);
                inner.removeFromLeft (12);

                if (item.favoriteKey.isNotEmpty())
                {
                    auto heart = inner.removeFromRight (browserHeartWidth).toFloat();
                    const bool fav = settings.isFavorite (item.favoriteKey);
                    drawIcon (g, fav ? Icon::heartFilled : Icon::heart, heart.withSizeKeepingCentre (22.0f, 22.0f),
                              fav ? t.record : t.textDim);
                }

                if (item.detail.isNotEmpty())
                {
                    g.setColour (t.text);
                    g.setFont (Theme::font (16.0f, isLoaded));
                    g.drawFittedText (item.label, inner.withY (inner.getY() + 8).withHeight (22),
                                      juce::Justification::centredLeft, 1, 0.85f);
                    g.setColour (t.textDim);
                    g.setFont (Theme::font (12.5f));
                    g.drawFittedText (item.detail, inner.withY (inner.getY() + 31).withHeight (16),
                                      juce::Justification::centredLeft, 1, 0.85f);
                }
                else
                {
                    g.setColour (t.text);
                    g.setFont (Theme::font (16.0f, isLoaded));
                    g.drawFittedText (item.label, inner, juce::Justification::centredLeft, 1, 0.85f);
                }
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            pressedRow = rowAt (e.y);
            longPress.mouseDown (e);
            repaint();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            longPress.mouseDrag (e);
            if (pressedRow >= 0 && e.getDistanceFromDragStart() > 8)
            {
                pressedRow = -1;
                repaint();
            }
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            const bool wasPressed = pressedRow >= 0;
            pressedRow = -1;
            repaint();

            if (longPress.mouseUp() || ! wasPressed || e.getDistanceFromDragStart() > 8)
                return;

            const int row = rowAt (e.y);
            if (row < 0)
                return;

            auto item = list.owner.items[(size_t) row]; // copy: activation may rebuild the list
            if (item.favoriteKey.isNotEmpty() && e.x >= getWidth() - browserHeartWidth - 10)
                list.owner.toggleFavorite (item);
            else
                list.owner.activate (item);
        }

        ItemList& list;
        LongPress longPress;
        int pressedRow = -1;
    };

    BrowserSheet& owner;
    juce::Viewport viewport;
    Content content;
    std::vector<int> tops;
    int contentHeight = 0;
};

//==============================================================================
// Pads under the list to try the loaded sound (DEV-3). Drum kits get eight pads (the bank of
// the selected pad); melodic instruments a row of scale notes from C3.
class BrowserSheet::AuditionPads : public juce::Component,
                                   private juce::Timer
{
public:
    explicit AuditionPads (BrowserSheet& o) : owner (o) { startTimerHz (20); }

    bool isDrum() const
    {
        auto* t = owner.track();
        return t != nullptr && t->isDrum();
    }

    int bank() const
    {
        auto* t = owner.track();
        return t != nullptr && owner.mode == Mode::sample ? t->perf.selectedPad / 8 : 0;
    }

    // Columns and rows for a given shape.
    int columns() const { return isDrum() || twoRows ? 4 : 8; }
    int rows() const { return isDrum() || twoRows ? 2 : 1; }

    int noteForCell (int row, int column) const
    {
        auto* t = owner.track();
        if (t == nullptr)
            return -1;

        if (t->isDrum())
            return drumBaseNote + bank() * 8 + (rows() - 1 - row) * 4 + column; // pad 0 bottom-left

        const int index = row * columns() + column;
        auto& set = owner.studio.getSet();
        if (set.inKey)
            return transposeInScale (samplerRootNote + set.root, index, true, set.root, set.scale);
        static const int major[] = { 0, 2, 4, 5, 7, 9, 11, 12 };
        return samplerRootNote + major[index & 7];
    }

    juce::Rectangle<float> cell (int row, int column) const
    {
        const float w = (float) getWidth() / (float) columns();
        const float h = (float) getHeight() / (float) rows();
        return juce::Rectangle<float> (column * w, row * h, w, h).reduced (3.0f);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = Theme::get();
        auto* tr = owner.track();
        if (tr == nullptr)
            return;

        const auto colour = Theme::trackColour (tr->colour);
        for (int r = 0; r < rows(); ++r)
            for (int c = 0; c < columns(); ++c)
            {
                const int note = noteForCell (r, c);
                auto area = cell (r, c);
                const bool lit = held.count (note) > 0 || owner.studio.isNoteSounding (owner.trackId, note);

                if (tr->isDrum())
                {
                    const int pad = note - drumBaseNote;
                    auto& p = tr->pads[(size_t) juce::jlimit (0, numDrumPads - 1, pad)];
                    const bool hasSample = p.sampleRef.isNotEmpty();
                    auto fill = hasSample ? colour.withMultipliedBrightness (0.45f).withMultipliedSaturation (0.8f) : t.surfaceRaised;
                    g.setColour (lit ? colour.brighter (0.2f) : fill);
                    g.fillRoundedRectangle (area, Theme::corner);
                    if (owner.mode == Mode::sample && tr->perf.selectedPad == pad)
                    {
                        g.setColour (t.text);
                        g.drawRoundedRectangle (area.reduced (1.0f), Theme::corner, 2.0f);
                    }
                    g.setColour (lit ? juce::Colours::black : t.text.withAlpha (0.8f));
                    g.setFont (Theme::font (11.5f));
                    g.drawFittedText (p.name.isNotEmpty() ? p.name : juce::String (pad + 1),
                                      area.reduced (6.0f, 4.0f).toNearestInt(), juce::Justification::bottomLeft, 1, 0.75f);
                }
                else
                {
                    const bool root = ((note - owner.studio.getSet().root) % 12 + 12) % 12 == 0;
                    g.setColour (lit ? colour : (root ? colour.withMultipliedBrightness (0.45f) : t.surfaceRaised));
                    g.fillRoundedRectangle (area, Theme::corner);
                    g.setColour (lit ? juce::Colours::black : t.text.withAlpha (0.85f));
                    g.setFont (Theme::font (12.0f));
                    g.drawText (noteName (note), area, juce::Justification::centred);
                }
            }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int c = juce::jlimit (0, columns() - 1, (int) (e.position.x / ((float) getWidth() / (float) columns())));
        const int r = juce::jlimit (0, rows() - 1, (int) (e.position.y / ((float) getHeight() / (float) rows())));
        const int note = noteForCell (r, c);
        if (note < 0)
            return;

        owner.stopAudition();

        // In the sample browser a drum pad tap also picks the pad that samples load into.
        if (owner.mode == Mode::sample && isDrum())
        {
            const int pad = note - drumBaseNote;
            const int id = owner.trackId;
            if (auto* t = owner.track(); t != nullptr && t->perf.selectedPad != pad)
                owner.studio.getDocument().editWithoutUndo ([id, pad] (Set& s) { if (auto* tr = s.findTrack (id)) tr->perf.selectedPad = pad; });
        }

        const float velocity = juce::jlimit (0.3f, 1.0f, 1.0f - (e.position.y - cell (r, c).getY()) / juce::jmax (1.0f, cell (r, c).getHeight()) * 0.6f);
        sources[e.source.getIndex()] = note;
        held.insert (note);
        owner.studio.noteOn (owner.trackId, note, velocity);
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (auto it = sources.find (e.source.getIndex()); it != sources.end())
        {
            owner.studio.noteOff (owner.trackId, it->second);
            held.erase (it->second);
            sources.erase (it);
            repaint();
        }
    }

    void releaseAll()
    {
        for (auto& [source, note] : sources)
            owner.studio.noteOff (owner.trackId, note);
        sources.clear();
        held.clear();
    }

    bool twoRows = false; // melodic pads as 2x4 (landscape)

private:
    void timerCallback() override { repaint(); }

    BrowserSheet& owner;
    std::map<int, int> sources;
    std::multiset<int> held;
};

//==============================================================================
BrowserSheet::BrowserSheet (Studio& s, int id, Mode m) : Sheet ("Browse"), studio (s), trackId (id), mode (m)
{
    heightFraction = 0.94f;

    if (auto* t = track())
    {
        original = *t;
        wasDrum = t->isDrum();
    }

    // One undo step for the whole browse session; closed by done()/cancel()/the destructor.
    studio.getDocument().beginGesture();

    closeButton.onClick = [this] { cancel(); };
    doneButton.onClick = [this] { done(); };
    addAndMakeVisible (doneButton);

    saveButton.onClick = [this] { saveUserPreset(); };
    filterButton.setTooltip ("Filter by instrument");

    saveButton.setTooltip ("Save as user preset");
    addChildComponent (saveButton);
    saveButton.setVisible (mode == Mode::trackPreset);

    filterButton.onClick = [this] { showTypeFilterMenu(); };
    addChildComponent (filterButton);
    filterButton.setVisible (mode == Mode::trackPreset);

    searchEditor.setTextToShowWhenEmpty (mode == Mode::sample ? "Search samples" : "Search", Theme::get().textDim);
    searchEditor.setFont (Theme::font (16.0f));
    searchEditor.setIndents (38, 0);
    searchEditor.setJustification (juce::Justification::centredLeft);
    searchEditor.setSelectAllWhenFocused (true);
    searchEditor.onTextChange = [this]
    {
        clearSearchButton.setVisible (searchEditor.getText().isNotEmpty());
        rebuildItems();
        list->scrollToTop();
    };
    searchEditor.onReturnKey = [this] { searchEditor.giveAwayKeyboardFocus(); };
    searchEditor.onEscapeKey = [this] { searchEditor.setText ({}); searchEditor.giveAwayKeyboardFocus(); };
    addAndMakeVisible (searchEditor);
    addAndMakeVisible (searchIcon);

    clearSearchButton.onClick = [this] { searchEditor.setText ({}); };
    addChildComponent (clearSearchButton);

    chipBar = std::make_unique<ChipBar>();
    chipBar->onSelect = [this] (int index)
    {
        chipIndex = index;
        chipBar->setSelected (index);
        if (searchEditor.getText().isNotEmpty())
            searchEditor.setText ({}, false);
        clearSearchButton.setVisible (false);
        rebuildItems();
        list->scrollToTop();
        list->scrollToKey (loadedKey());
    };
    addAndMakeVisible (*chipBar);

    list = std::make_unique<ItemList> (*this);
    addAndMakeVisible (*list);

    pads = std::make_unique<AuditionPads> (*this);
    addAndMakeVisible (*pads);

    reloadUserPresets();
    buildChips();
    updateTitle();
    rebuildItems();

    studio.addListener (this);
}

BrowserSheet::~BrowserSheet()
{
    studio.removeListener (this);
    pads->releaseAll();
    stopAudition();

    // Dismissed without Done or Cancel (tap outside, view change): keep what is loaded.
    if (! finished)
        studio.getDocument().endGesture();
}

const Track* BrowserSheet::track() const
{
    return studio.getSet().findTrack (trackId);
}

//==============================================================================
void BrowserSheet::done()
{
    if (finished)
        return;
    finished = true;
    pads->releaseAll();
    stopAudition();
    studio.getDocument().endGesture();
    close();
}

void BrowserSheet::cancel()
{
    if (finished)
        return;
    finished = true;
    pads->releaseAll();
    stopAudition();

    // Restore inside the gesture, so the session stays a single undo step.
    if (edited && track() != nullptr)
    {
        const auto restored = original;
        studio.getDocument().edit ([restored] (Set& s)
        {
            if (auto* t = s.findTrack (restored.id))
                *t = restored;
        });
    }

    studio.getDocument().endGesture();
    close();
}

void BrowserSheet::applyEdit (std::function<void (Track&)> change)
{
    const int id = trackId;
    edited = true;
    studio.getDocument().edit ([id, change] (Set& s)
    {
        if (auto* t = s.findTrack (id))
            change (*t);
    });
}

//==============================================================================
void BrowserSheet::buildChips()
{
    chips.clear();
    auto* t = track();
    juce::String initial;

    switch (mode)
    {
        case Mode::trackPreset:
            chips = getPresetCategories();
            chips.add ("User");
            chips.add ("Favorites");
            if (t != nullptr)
            {
                const bool isUser = std::any_of (userPresets.begin(), userPresets.end(),
                                                 [t] (const TrackPreset& p) { return p.name == t->presetName; })
                                    && findFactoryPreset (t->presetName) == nullptr;
                initial = isUser ? juce::String ("User")
                                 : (t->presetCategory.isNotEmpty() ? t->presetCategory
                                                                   : (t->isDrum() ? "Drums" : t->instrumentType == InstrumentType::sampler ? "Sampler" : "Keys"));
            }
            break;

        case Mode::effect1:
        case Mode::effect2:
        {
            for (int type = 1; type < numEffectTypes; ++type)
                chips.add (getEffectDisplayName ((EffectType) type));
            chips.add ("None");
            chips.add ("Favorites");
            if (t != nullptr)
            {
                const auto type = (EffectType) t->effects[mode == Mode::effect1 ? 0 : 1].type;
                initial = type == EffectType::none ? juce::String ("None") : getEffectDisplayName (type);
            }
            break;
        }

        case Mode::sample:
        {
            chips = { "Factory", "Recordings", "This Set" };
            if (t != nullptr)
            {
                const auto ref = t->isDrum() ? t->pads[(size_t) t->perf.selectedPad].sampleRef : t->samplerSampleRef;
                initial = ref.startsWith ("user:") ? "Recordings" : ref.startsWith ("set:") ? "This Set" : "Factory";
            }
            break;
        }
    }

    chipIndex = juce::jmax (0, chips.indexOf (initial));
    chipBar->setChips (chips, chipIndex);
}

void BrowserSheet::selectChip (const juce::String& label)
{
    const int index = chips.indexOf (label);
    if (index >= 0 && chipBar->onSelect != nullptr)
        chipBar->onSelect (index);
}

void BrowserSheet::reloadUserPresets()
{
    userPresets = loadUserPresets (studio.getUserPresetsFolder());
}

void BrowserSheet::updateTitle()
{
    auto* t = track();
    juce::String newTitle;
    switch (mode)
    {
        case Mode::trackPreset: newTitle = "Sounds"; break;
        case Mode::effect1:
        case Mode::effect2:
        {
            const int slot = mode == Mode::effect1 ? 0 : 1;
            const bool drum = t != nullptr && t->isDrum();
            newTitle = juce::String ("Effects: ") + (drum ? (slot == 0 ? "Pad FX" : "Kit FX") : (slot == 0 ? "FX1" : "FX2"));
            break;
        }
        case Mode::sample:
            if (t != nullptr && t->isDrum())
            {
                newTitle = "Samples: Pad " + juce::String (t->perf.selectedPad + 1);
            }
            else
            {
                newTitle = "Samples: Sampler";
            }
            break;
    }

    if (newTitle != title)
    {
        title = newTitle;
        repaint();
    }
}

juce::String BrowserSheet::loadedKey() const
{
    auto* t = track();
    if (t == nullptr)
        return {};

    switch (mode)
    {
        case Mode::trackPreset:
            return browserTrackFavoriteKey (t->presetName);
        case Mode::effect1:
        case Mode::effect2:
        {
            auto& fx = t->effects[mode == Mode::effect1 ? 0 : 1];
            if (fx.type == (int) EffectType::none)
                return "fx:none";
            return browserEffectFavoriteKey ((EffectType) fx.type, fx.presetName);
        }
        case Mode::sample:
            return t->isDrum() ? t->pads[(size_t) t->perf.selectedPad].sampleRef : t->samplerSampleRef;
    }
    return {};
}

juce::String BrowserSheet::emptyMessage() const
{
    const auto chip = chips[chipIndex];
    if (searchEditor.getText().trim().isNotEmpty())
        return "Nothing matches \"" + searchEditor.getText().trim() + "\"";
    if (chip == "Favorites")
        return "No favorites yet.\nTap the heart next to a preset to add it here.";
    if (mode == Mode::trackPreset && chip == "User")
        return "No user presets yet.\nSave the current sound with the save button above.";
    if (mode == Mode::sample && chip == "Recordings")
        return "No saved recordings yet.\nRecordings saved from the Sample tab and imported files appear here.";
    if (mode == Mode::sample && chip == "This Set")
        return "No samples in this set yet.\nRecord one from the Sample tab.";
    if (typeFilter >= 0)
        return "No " + getInstrumentDisplayName ((InstrumentType) typeFilter) + " presets here.";
    return "Nothing here yet.";
}

//==============================================================================
void BrowserSheet::rebuildItems()
{
    items.clear();
    shownPresets.clear();
    shownEffects.clear();

    const auto search = searchEditor.getText().trim();
    const bool searching = search.isNotEmpty();
    const auto chip = chips[chipIndex];
    auto& settings = studio.getSettings();

    auto addHeader = [this] (const juce::String& label)
    {
        Item h;
        h.kind = Item::Kind::header;
        h.label = label;
        items.push_back (h);
    };

    if (mode == Mode::trackPreset)
    {
        std::vector<TrackPreset> pool;
        bool grouped = true;

        if (searching || chip == "Favorites")
        {
            pool = getFactoryPresets();
            pool.insert (pool.end(), userPresets.begin(), userPresets.end());
        }
        else if (chip == "User")
        {
            pool = userPresets;
        }
        else
        {
            pool = getFactoryPresets();
            grouped = false;
        }

        auto indices = filterPresets (pool, (searching || chip == "User" || chip == "Favorites") ? juce::String() : chip,
                                      search, typeFilter);

        for (auto i : indices)
        {
            auto& p = pool[(size_t) i];
            if (chip == "Favorites" && ! searching && ! settings.isFavorite (browserTrackFavoriteKey (p.name)))
                continue;
            shownPresets.push_back (p);
        }

        // Grouped views: by category in browser order, then anything uncategorised.
        if (grouped)
            std::stable_sort (shownPresets.begin(), shownPresets.end(), [] (const TrackPreset& a, const TrackPreset& b)
            {
                auto rank = [] (const juce::String& c) { const int r = getPresetCategories().indexOf (c); return r < 0 ? 1000 : r; };
                return rank (a.category) < rank (b.category);
            });

        juce::String lastCategory = "\x01";
        for (size_t i = 0; i < shownPresets.size(); ++i)
        {
            auto& p = shownPresets[i];
            if (grouped && p.category != lastCategory)
            {
                addHeader (p.category.isNotEmpty() ? p.category : "Other");
                lastCategory = p.category;
            }

            Item item;
            item.kind = Item::Kind::preset;
            item.label = p.name;
            item.detail = getInstrumentDisplayName (p.type) + (p.isUser ? " - User" : "");
            item.icon = browserIconFor (p.type);
            item.key = browserTrackFavoriteKey (p.name);
            item.favoriteKey = browserTrackFavoriteKey (p.name);
            item.index = (int) i;
            item.userPreset = p.isUser;
            items.push_back (item);
        }
    }
    else if (mode == Mode::effect1 || mode == Mode::effect2)
    {
        if (chip == "None" && ! searching)
        {
            Item item;
            item.kind = Item::Kind::clearEffect;
            item.label = "Empty Slot";
            item.detail = "Remove the effect from this slot";
            item.icon = Icon::close;
            item.key = "fx:none";
            items.push_back (item);
            return;
        }

        const auto chipType = chips.indexOf (chip) + 1; // effect chips follow EffectType order
        for (auto& p : getFactoryEffectPresets())
        {
            if (p.type == EffectType::none)
                continue;
            const auto key = browserEffectFavoriteKey (p.type, p.name);
            if (searching ? ! (matchesSearch (p.name, search) || matchesSearch (getEffectDisplayName (p.type), search))
                          : (chip == "Favorites" ? ! settings.isFavorite (key) : (int) p.type != chipType))
                continue;
            shownEffects.push_back (p);
        }

        const bool grouped = searching || chip == "Favorites";
        if (grouped)
            std::stable_sort (shownEffects.begin(), shownEffects.end(),
                              [] (const EffectPreset& a, const EffectPreset& b) { return (int) a.type < (int) b.type; });

        int lastType = -1;
        for (size_t i = 0; i < shownEffects.size(); ++i)
        {
            auto& p = shownEffects[i];
            if (grouped && (int) p.type != lastType)
            {
                addHeader (getEffectDisplayName (p.type));
                lastType = (int) p.type;
            }

            Item item;
            item.kind = Item::Kind::effect;
            item.label = p.name;
            item.icon = Icon::filter;
            item.key = browserEffectFavoriteKey (p.type, p.name);
            item.favoriteKey = item.key;
            item.index = (int) i;
            items.push_back (item);
        }
    }
    else // samples
    {
        auto addSample = [&] (const juce::String& ref, const juce::String& name, const juce::String& detail)
        {
            if (searching && ! matchesSearch (name, search) && ! matchesSearch (detail, search))
                return;
            Item item;
            item.kind = Item::Kind::sample;
            item.label = name;
            item.detail = detail;
            item.icon = Icon::waveform;
            item.key = ref;
            item.ref = ref;
            items.push_back (item);
        };

        if (chip == "Factory")
        {
            // Grouped by category, in the order the categories first appear.
            juce::StringArray categories;
            for (auto& info : FactorySounds::list())
                categories.addIfNotAlreadyThere (info.category);

            for (auto& category : categories)
            {
                const size_t before = items.size();
                addHeader (category);
                for (auto& info : FactorySounds::list())
                    if (info.category == category)
                        addSample ("factory:" + info.id, info.name, {});
                if (items.size() == before + 1)
                    items.pop_back(); // no matches in this category
            }
        }
        else if (chip == "Recordings")
        {
            for (auto& ref : studio.getEngine().getSamples().listUserSamples())
                addSample (ref, sampleDisplayName (ref),
                           ref.fromFirstOccurrenceOf (":", false, false).startsWith ("rec-") ? "Saved recording" : "Imported");
        }
        else
        {
            for (auto& ref : listSetSamples (studio.getDocument().getFolder()))
                addSample (ref, sampleDisplayName (ref), isSetRecording (ref) ? "Recorded in this set" : "In this set");
        }
    }

    list->refresh();
}

//==============================================================================
int BrowserSheet::auditionNote() const
{
    auto* t = track();
    if (t == nullptr)
        return -1;
    if (t->isDrum())
        return drumBaseNote + (mode == Mode::sample ? t->perf.selectedPad : 0);
    return samplerRootNote;
}

void BrowserSheet::audition (int note, int durationMs)
{
    stopAudition();
    if (note < 0)
        return;
    playingNote = note;
    studio.noteOn (trackId, note, 0.8f);
    startTimer (durationMs);
}

void BrowserSheet::stopAudition()
{
    stopTimer();
    if (playingNote >= 0)
        studio.noteOff (trackId, playingNote);
    playingNote = -1;
}

void BrowserSheet::timerCallback()
{
    stopAudition();
}

void BrowserSheet::activate (const Item& item)
{
    if (track() == nullptr)
        return;

    switch (item.kind)
    {
        case Item::Kind::preset:
        {
            if (! juce::isPositiveAndBelow (item.index, (int) shownPresets.size()))
                return;
            const auto preset = shownPresets[(size_t) item.index];
            pads->releaseAll();
            applyEdit ([preset] (Track& t) { applyPreset (t, preset); });
            audition (preset.type == InstrumentType::drumKit ? drumBaseNote : samplerRootNote, 300);
            break;
        }

        case Item::Kind::effect:
        {
            if (! juce::isPositiveAndBelow (item.index, (int) shownEffects.size()))
                return;
            const auto preset = shownEffects[(size_t) item.index];
            const int slot = mode == Mode::effect1 ? 0 : 1;
            applyEdit ([preset, slot] (Track& t) { applyEffectPreset (t, slot, preset); });
            audition (auditionNote(), 400);
            break;
        }

        case Item::Kind::clearEffect:
        {
            const int slot = mode == Mode::effect1 ? 0 : 1;
            applyEdit ([slot] (Track& t)
            {
                EffectPreset none;
                applyEffectPreset (t, slot, none);
                t.presetModified = true;
            });
            break;
        }

        case Item::Kind::sample:
        {
            const auto ref = item.ref;
            const auto name = item.label;
            applyEdit ([ref, name] (Track& t)
            {
                if (t.isDrum())
                {
                    auto& pad = t.pads[(size_t) t.perf.selectedPad];
                    pad.sampleRef = ref;
                    pad.name = name;
                }
                else
                {
                    t.samplerSampleRef = ref;
                }
                t.presetModified = true;
            });
            audition (auditionNote(), 300);
            break;
        }

        case Item::Kind::header:
            break;
    }

    list->repaint();
    for (auto* c : list->getChildren())
        c->repaint();
}

void BrowserSheet::toggleFavorite (const Item& item)
{
    if (item.favoriteKey.isEmpty())
        return;
    studio.getSettings().toggleFavorite (item.favoriteKey);
    if (chips[chipIndex] == "Favorites" && searchEditor.getText().trim().isEmpty())
        rebuildItems();
    else
        list->refresh();
}

void BrowserSheet::showItemMenu (const Item& item, juce::Component& target)
{
    if (item.kind != Item::Kind::preset || ! item.userPreset)
        return;

    const auto name = item.label;
    juce::PopupMenu menu;
    menu.addSectionHeader (name);
    menu.addItem (1, "Delete Preset");

    juce::Component::SafePointer<BrowserSheet> safe (this);
    showMenu (menu, target, [safe, name] (int result)
    {
        if (safe == nullptr || result != 1)
            return;
        safe->studio.getUserPresetsFolder().getChildFile (juce::File::createLegalFileName (name) + ".json").deleteFile();
        safe->reloadUserPresets();
        safe->rebuildItems();
    });
}

void BrowserSheet::showTypeFilterMenu()
{
    juce::PopupMenu menu;
    menu.addItem (100, "All Instruments", true, typeFilter < 0);
    for (int i = 0; i < numInstrumentTypes; ++i)
        menu.addItem (101 + i, getInstrumentDisplayName ((InstrumentType) i), true, typeFilter == i);

    juce::Component::SafePointer<BrowserSheet> safe (this);
    showMenu (menu, filterButton, [safe] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        safe->typeFilter = result - 101;
        safe->filterButton.setToggleState (safe->typeFilter >= 0, juce::dontSendNotification);
        safe->filterButton.setButtonText (safe->typeFilter >= 0 ? getInstrumentDisplayName ((InstrumentType) safe->typeFilter)
                                                                : juce::String ("All"));
        safe->rebuildItems();
    });
}

void BrowserSheet::saveUserPreset()
{
    auto* t = track();
    if (t == nullptr)
        return;

    juce::Component::SafePointer<BrowserSheet> safe (this);
    askForText ("Save Preset", t->presetName, [safe] (juce::String name)
    {
        if (safe == nullptr || name.isEmpty())
            return;
        auto* tr = safe->track();
        if (tr == nullptr)
            return;

        auto preset = presetFromTrack (*tr, name);
        if (! rb::saveUserPreset (safe->studio.getUserPresetsFolder(), preset))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save Preset",
                                                    "The preset could not be saved.");
            return;
        }

        safe->applyEdit ([name] (Track& renamed)
        {
            renamed.presetName = name;
            renamed.instrument.presetName = name;
            renamed.presetModified = false;
        });
        safe->reloadUserPresets();
        safe->selectChip ("User");
    });
}

//==============================================================================
void BrowserSheet::studioChanged()
{
    auto* t = track();
    if (t == nullptr)
        return;

    updateTitle();
    if (t->isDrum() != wasDrum)
    {
        wasDrum = t->isDrum();
        pads->releaseAll();
        resized();
    }
    pads->repaint();
    list->refresh();
}

void BrowserSheet::paint (juce::Graphics& g)
{
    Sheet::paint (g);
}

void BrowserSheet::resized()
{
    Sheet::resized();

    // Title bar: [title ............ save  Done  X]
    {
        auto bar = getLocalBounds().removeFromTop (Theme::barHeight).withTrimmedRight (Theme::touch + 12);
        doneButton.setBounds (bar.removeFromRight (78).withSizeKeepingCentre (78, 38));
        bar.removeFromRight (4);
        if (saveButton.isVisible())
            saveButton.setBounds (bar.removeFromRight (Theme::touch).withSizeKeepingCentre (Theme::touch, Theme::touch));
    }

    auto area = getContentArea();
    const bool landscape = area.getWidth() > area.getHeight() * 1.25f;

    auto searchRow = area.removeFromTop (browserSearchHeight);
    if (filterButton.isVisible())
    {
        filterButton.setBounds (searchRow.removeFromRight (96).reduced (0, 2));
        searchRow.removeFromRight (6);
    }
    searchEditor.setBounds (searchRow);
    searchIcon.setBounds (searchRow.withWidth (38).withSizeKeepingCentre (22, 22).translated (4, 0));
    clearSearchButton.setBounds (searchRow.removeFromRight (Theme::touch).reduced (6));
    clearSearchButton.toFront (false);
    searchIcon.toFront (false);

    area.removeFromTop (8);
    chipBar->setBounds (area.removeFromTop (browserChipHeight));
    area.removeFromTop (6);

    auto* t = track();
    const bool drum = t != nullptr && t->isDrum();

    if (landscape)
    {
        auto right = area.removeFromRight (juce::jmin (300, area.getWidth() * 2 / 5));
        area.removeFromRight (10);
        pads->twoRows = true;
        const int padHeight = juce::jmin (right.getHeight(), right.getWidth() * 3 / 4);
        pads->setBounds (right.withHeight (padHeight).withY (right.getBottom() - padHeight));
        list->setBounds (area);
        return;
    }

    pads->twoRows = false;
    const int padHeight = drum ? juce::jmin (112, area.getHeight() / 4) : juce::jmin (60, area.getHeight() / 6);
    pads->setBounds (area.removeFromBottom (padHeight));
    area.removeFromBottom (8);
    list->setBounds (area);

    if (! scrolledToLoaded && area.getHeight() > 0)
    {
        scrolledToLoaded = true;
        list->scrollToKey (loadedKey());
    }
}

} // namespace rb
