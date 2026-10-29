namespace rb
{

namespace
{
    constexpr int headerHeight = 64;
    constexpr int mixerHeight = 210;
    constexpr int columnWidth = 92;
    constexpr int sceneWidth = 40;
    constexpr int minRowHeight = 46;

    juce::String instrumentLetter (const Track& t)
    {
        switch (t.instrumentType)
        {
            case InstrumentType::drumKit: return "Drums";
            case InstrumentType::sampler: return "Sampler";
            case InstrumentType::wave:    return "Wave";
            case InstrumentType::analog:
            default:                      return "Analog";
        }
    }

    Icon instrumentIcon (InstrumentType type)
    {
        switch (type)
        {
            case InstrumentType::drumKit: return Icon::drum;
            case InstrumentType::sampler: return Icon::sampler;
            case InstrumentType::wave:    return Icon::wave;
            case InstrumentType::analog:
            default:                      return Icon::synth;
        }
    }

    // Draws a tiny picture of a clip's notes.
    void drawClipPreview (juce::Graphics& g, const Clip& clip, juce::Rectangle<float> area, juce::Colour colour)
    {
        if (clip.notes.empty() || clip.loopLength <= 0.0)
            return;

        int lo = 127, hi = 0;
        for (auto& n : clip.notes)
        {
            lo = juce::jmin (lo, n.pitch);
            hi = juce::jmax (hi, n.pitch);
        }
        const float range = (float) juce::jmax (1, hi - lo);

        g.setColour (colour);
        for (auto& n : clip.notes)
        {
            if (n.start < clip.loopStart || n.start >= clip.loopEnd())
                continue;
            const float x = area.getX() + (float) ((n.start - clip.loopStart) / clip.loopLength) * area.getWidth();
            const float w = juce::jmax (1.5f, (float) (n.length / clip.loopLength) * area.getWidth());
            const float y = area.getBottom() - ((n.pitch - lo) / range) * (area.getHeight() - 2.0f) - 2.0f;
            g.fillRect (x, y, w, 2.0f);
        }
    }
}

//==============================================================================
// One track: header, eight clip slots, and a mixer strip.
class SessionView::TrackColumn : public juce::Component
{
public:
    TrackColumn (SessionView& o, int id) : owner (o), trackId (id)
    {
        addAndMakeVisible (header);
        for (int s = 0; s < numScenes; ++s)
        {
            cells.push_back (std::make_unique<Cell> (*this, s));
            addAndMakeVisible (*cells.back());
        }
        addChildComponent (mixer);
    }

    void setMixerVisible (bool b) { mixer.setVisible (b); resized(); }

    void resized() override
    {
        auto bounds = getLocalBounds();
        header.setBounds (bounds.removeFromTop (headerHeight));
        if (mixer.isVisible())
            mixer.setBounds (bounds.removeFromBottom (mixerHeight));

        const int rowHeight = bounds.getHeight() / numScenes;
        for (auto& c : cells)
            c->setBounds (bounds.removeFromTop (rowHeight));
    }

    void update()
    {
        header.repaint();
        for (auto& c : cells)
            c->repaint();
        mixer.update();
    }

    const Track* track() const { return owner.studio.getSet().findTrack (trackId); }

    int getSlotAt (juce::Point<int> pointInColumn) const
    {
        for (auto& c : cells)
            if (c->getBounds().contains (pointInColumn))
                return c->slot;
        return -1;
    }

    //==========================================================================
    struct Header : public juce::Component
    {
        explicit Header (TrackColumn& c) : column (c)
        {
            longPress.onLongPress = [this] (juce::Point<int>) { pressed = true; repaint(); };
        }

        void paint (juce::Graphics& g) override
        {
            const auto& t = Theme::get();
            auto* track = column.track();
            if (track == nullptr)
                return;

            auto bounds = getLocalBounds().toFloat().reduced (3.0f, 4.0f);
            const auto colour = Theme::trackColour (track->colour);
            g.setColour (pressed ? t.surfaceRaised.brighter (0.1f) : t.surface);
            g.fillRoundedRectangle (bounds, 8.0f);
            g.setColour (colour);
            g.fillRoundedRectangle (bounds.removeFromTop (4.0f), 2.0f);

            auto iconArea = bounds.removeFromTop (24.0f).reduced (4.0f, 2.0f);
            drawIcon (g, instrumentIcon (track->instrumentType), iconArea.removeFromLeft (20.0f), colour);
            g.setColour (t.textDim);
            g.setFont (Theme::font (11.0f));
            g.drawText (instrumentLetter (*track), iconArea.withTrimmedLeft (4.0f), juce::Justification::centredLeft);

            g.setColour (t.text);
            g.setFont (Theme::font (13.0f, true));
            g.drawFittedText (track->name + (track->presetModified ? "*" : ""), bounds.reduced (6.0f, 0.0f).toNearestInt(),
                              juce::Justification::centredLeft, 2, 0.8f);

            if (track->mute || ! isAudible (*track))
            {
                g.setColour (t.background.withAlpha (0.5f));
                g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (3.0f, 4.0f), 8.0f);
            }
        }

        bool isAudible (const Track& track) const
        {
            for (auto& other : column.owner.studio.getSet().tracks)
                if (other.solo)
                    return track.solo && ! track.mute;
            return ! track.mute;
        }

        void mouseDown (const juce::MouseEvent& e) override { longPress.mouseDown (e); }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            longPress.mouseDrag (e);
            if (longPress.hasFired())
            {
                if (! dragging && e.getDistanceFromDragStart() > 12)
                {
                    dragging = true;
                    column.owner.beginDrag (DragState::Kind::track, column.trackId, 0, e.getScreenPosition());
                }
                if (dragging)
                    column.owner.updateDrag (e.getScreenPosition());
            }
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            const bool wasLong = longPress.mouseUp();
            pressed = false;
            repaint();

            if (dragging)
            {
                dragging = false;
                column.owner.endDrag (e.getScreenPosition());
                return;
            }

            if (wasLong)
                showMenu();
            else if (e.mouseWasClicked())
                column.owner.nav.showInstrument (column.trackId, -1); // TRK-3
        }

        void showMenu()
        {
            auto& studio = column.owner.studio;
            const int id = column.trackId;
            juce::PopupMenu menu;
            menu.addItem (1, "Duplicate", studio.getSet().tracks.size() < (size_t) maxTracks);
            menu.addItem (2, "Delete", studio.getSet().tracks.size() > 1);

            rb::showMenu (menu, *this, [&studio, id] (int result)
            {
                if (result == 1) studio.getDocument().edit ([id] (Set& s) { duplicateTrack (s, id); });
                if (result == 2) studio.getDocument().edit ([id] (Set& s) { deleteTrack (s, id); });
            });
        }

        TrackColumn& column;
        LongPress longPress;
        bool pressed = false, dragging = false;
    };

    //==========================================================================
    struct Cell : public juce::Component
    {
        Cell (TrackColumn& c, int s) : column (c), slot (s)
        {
            longPress.onLongPress = [this] (juce::Point<int>) { repaint(); };
        }

        const Clip* clip() const
        {
            auto* t = column.track();
            return t != nullptr ? t->slots[(size_t) slot].get() : nullptr;
        }

        juce::Rectangle<float> stopArea() const
        {
            auto b = getLocalBounds().toFloat().reduced (3.0f);
            return b.removeFromRight (juce::jmin (36.0f, b.getHeight())).reduced (6.0f);
        }

        void paint (juce::Graphics& g) override
        {
            const auto& t = Theme::get();
            auto* track = column.track();
            if (track == nullptr)
                return;

            auto& studio = column.owner.studio;
            const auto colour = Theme::trackColour (track->colour);
            auto bounds = getLocalBounds().toFloat().reduced (3.0f);
            auto* c = clip();

            const int playing = studio.getPlayingSlot (column.trackId);
            const int queued = studio.getQueuedSlot (column.trackId);
            const bool isPlaying = playing == slot && c != nullptr;
            const bool isQueued = queued == slot;
            const bool stopQueued = isPlaying && queued == SessionProcessor::queuedStop;

            if (c == nullptr)
            {
                g.setColour (t.surface);
                g.fillRoundedRectangle (bounds, 7.0f);
                g.setColour (isQueued ? colour : t.outline.withAlpha (0.5f));
                g.drawRoundedRectangle (bounds.reduced (0.5f), 7.0f, 1.0f);
                drawIcon (g, Icon::plus, bounds.withSizeKeepingCentre (16.0f, 16.0f), t.textDim.withAlpha (0.5f), 1.4f);
                return;
            }

            const bool blink = isQueued && (juce::Time::getMillisecondCounter() / 250) % 2 == 0;
            auto fill = isPlaying ? colour : colour.withMultipliedSaturation (0.75f).withMultipliedBrightness (0.55f);
            if (blink) fill = fill.brighter (0.4f);
            if (longPress.hasFired()) fill = fill.brighter (0.2f);

            g.setColour (fill);
            g.fillRoundedRectangle (bounds, 7.0f);

            auto previewArea = bounds.reduced (6.0f, 6.0f).withTrimmedBottom (14.0f).withTrimmedRight (isPlaying ? 26.0f : 0.0f);
            drawClipPreview (g, *c, previewArea, juce::Colours::black.withAlpha (isPlaying ? 0.55f : 0.45f));

            if (isPlaying)
            {
                // Progress through the loop.
                const double position = studio.getClipPosition (column.trackId);
                const float progress = (float) juce::jlimit (0.0, 1.0, (position - c->loopStart) / juce::jmax (0.001, c->loopLength));
                g.setColour (juce::Colours::black.withAlpha (0.25f));
                g.fillRect (bounds.getX(), bounds.getBottom() - 4.0f, bounds.getWidth() * progress, 4.0f);

                auto stop = stopArea();
                g.setColour (juce::Colours::black.withAlpha (stopQueued && blink ? 0.3f : 0.65f));
                g.fillRoundedRectangle (stop.withSizeKeepingCentre (14.0f, 14.0f), 2.0f);
            }

            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.setFont (Theme::font (11.0f, true));
            const double bars = c->loopLength / beatsPerBar;
            g.drawText (juce::String (bars, juce::exactlyEqual (bars, std::round (bars)) ? 0 : 1) + (juce::exactlyEqual (bars, 1.0) ? " bar" : " bars"),
                        bounds.reduced (6.0f, 4.0f), juce::Justification::bottomLeft);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (clip() != nullptr)
                longPress.mouseDown (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            longPress.mouseDrag (e);
            if (longPress.hasFired())
            {
                if (! dragging && e.getDistanceFromDragStart() > 12)
                {
                    dragging = true;
                    column.owner.beginDrag (DragState::Kind::clip, column.trackId, slot, e.getScreenPosition());
                }
                if (dragging)
                    column.owner.updateDrag (e.getScreenPosition());
            }
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            const bool wasLong = longPress.mouseUp();
            auto& studio = column.owner.studio;
            repaint();

            if (dragging)
            {
                dragging = false;
                column.owner.endDrag (e.getScreenPosition());
                return;
            }

            if (wasLong)
            {
                showMenu();
                return;
            }

            if (! e.mouseWasClicked())
                return;

            if (clip() == nullptr)
            {
                column.owner.nav.showInstrument (column.trackId, slot); // CLIP-1
                return;
            }

            if (e.getNumberOfClicks() >= 2)
            {
                column.owner.nav.showInstrument (column.trackId, slot); // CLIP-5
                return;
            }

            const bool playing = studio.getPlayingSlot (column.trackId) == slot;
            if (playing && stopArea().expanded (6.0f).contains (e.position))
                studio.stopClip (column.trackId); // CLIP-3
            else
                studio.launchClip (column.trackId, slot); // CLIP-2
        }

        void showMenu()
        {
            auto& studio = column.owner.studio;
            auto& nav = column.owner.nav;
            const int id = column.trackId;
            const int s = slot;

            juce::PopupMenu menu;
            menu.addItem (1, "Edit Clip");
            menu.addItem (2, "Duplicate");
            menu.addItem (3, "Delete");

            rb::showMenu (menu, *this, [&studio, &nav, id, s] (int result)
            {
                if (result == 1) nav.showInstrument (id, s);
                if (result == 2) studio.getDocument().edit ([=] (Set& set) { duplicateClip (set, id, s); });
                if (result == 3) studio.getDocument().edit ([=] (Set& set) { deleteClip (set, id, s); });
            });
        }

        TrackColumn& column;
        int slot;
        LongPress longPress;
        bool dragging = false;
    };

    //==========================================================================
    struct Mixer : public juce::Component
    {
        explicit Mixer (TrackColumn& c) : column (c)
        {
            addAndMakeVisible (volume);
            addAndMakeVisible (mute);
            addAndMakeVisible (solo);

            volume.setSliderStyle (juce::Slider::LinearVertical);
            volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            volume.setRange (0.0, 1.0);
            volume.setDoubleClickReturnValue (true, 0.8);
            volume.onDragStart = [this] { column.owner.studio.getDocument().beginGesture(); };
            volume.onDragEnd = [this] { column.owner.studio.getDocument().endGesture(); };
            volume.onValueChange = [this]
            {
                const float v = (float) volume.getValue();
                const int id = column.trackId;
                if (auto* t = column.track(); t != nullptr && ! juce::exactlyEqual (t->volume, v))
                    column.owner.studio.getDocument().edit ([id, v] (Set& s) { if (auto* tr = s.findTrack (id)) tr->volume = v; });
            };

            mute.setClickingTogglesState (false);
            solo.setClickingTogglesState (false);
            mute.onClick = [this]
            {
                const int id = column.trackId;
                column.owner.studio.getDocument().edit ([id] (Set& s) { if (auto* t = s.findTrack (id)) t->mute = ! t->mute; });
            };
            solo.onClick = [this]
            {
                const int id = column.trackId;
                column.owner.studio.getDocument().edit ([id] (Set& s) { if (auto* t = s.findTrack (id)) t->solo = ! t->solo; });
            };
        }

        void update()
        {
            auto* t = column.track();
            if (t == nullptr)
                return;
            volume.setValue (t->volume, juce::dontSendNotification);
            volume.setColour (juce::Slider::trackColourId, Theme::trackColour (t->colour));
            mute.setToggleState (t->mute, juce::dontSendNotification);
            solo.setToggleState (t->solo, juce::dontSendNotification);
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& t = Theme::get();
            g.setColour (t.surface);
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (3.0f), 8.0f);

            // Level meter beside the fader.
            if (auto* state = column.owner.studio.getSession().findTrackState (column.trackId))
            {
                const float level = juce::jlimit (0.0f, 1.0f, state->level.load());
                const float db = juce::Decibels::gainToDecibels (level, -60.0f);
                const float h = (db + 60.0f) / 60.0f;
                auto meter = meterArea.toFloat();
                g.setColour (t.surfaceRaised);
                g.fillRoundedRectangle (meter, 2.0f);
                g.setColour (level > 0.99f ? t.record : t.playing);
                g.fillRoundedRectangle (meter.withTop (meter.getBottom() - meter.getHeight() * h), 2.0f);
            }

            if (auto* tr = column.track())
            {
                g.setColour (t.textDim);
                g.setFont (Theme::font (11.0f));
                const float dbValue = volumeToDecibels (tr->volume);
                g.drawText (dbValue <= -99.0f ? juce::String ("-inf") : juce::String (dbValue, 1) + " dB",
                            getLocalBounds().removeFromTop (20), juce::Justification::centred);
            }
        }

        void resized() override
        {
            auto bounds = getLocalBounds().reduced (8, 6);
            bounds.removeFromTop (14);
            auto buttons = bounds.removeFromBottom (34);
            mute.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2));
            solo.setBounds (buttons.reduced (2));
            bounds.removeFromBottom (4);
            meterArea = bounds.removeFromRight (6);
            volume.setBounds (bounds);
        }

        TrackColumn& column;
        juce::Slider volume;
        PillButton mute { "M" }, solo { "S" };
        juce::Rectangle<int> meterArea;
    };

    SessionView& owner;
    int trackId;
    Header header { *this };
    std::vector<std::unique_ptr<Cell>> cells;
    Mixer mixer { *this };
};

//==============================================================================
class SessionView::SceneColumn : public juce::Component
{
public:
    explicit SceneColumn (SessionView& o) : owner (o)
    {
        longPress.onLongPress = [this] (juce::Point<int>) { repaint(); };
    }

    void setGeometry (int top, int rowHeight) { gridTop = top; rowH = rowHeight; repaint(); }

    int sceneAt (int y) const
    {
        if (y < gridTop || rowH <= 0)
            return -1;
        const int s = (y - gridTop) / rowH;
        return s < numScenes ? s : -1;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = Theme::get();
        for (int s = 0; s < numScenes; ++s)
        {
            auto cell = juce::Rectangle<int> (0, gridTop + s * rowH, getWidth(), rowH).toFloat().reduced (3.0f);
            const bool empty = sceneIsEmpty (owner.studio.getSet(), s);
            g.setColour (pressedScene == s ? t.outline : t.surface);
            g.fillRoundedRectangle (cell, 7.0f);
            g.setColour (empty ? t.textDim.withAlpha (0.5f) : t.text);
            g.setFont (Theme::font (15.0f, true));
            g.drawText (juce::String (s + 1), cell, juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        pressedScene = sceneAt (e.y);
        longPress.mouseDown (e);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        longPress.mouseDrag (e);
        if (longPress.hasFired() && pressedScene >= 0)
        {
            if (! dragging && e.getDistanceFromDragStart() > 12)
            {
                dragging = true;
                owner.beginDrag (DragState::Kind::scene, 0, pressedScene, e.getScreenPosition());
            }
            if (dragging)
                owner.updateDrag (e.getScreenPosition());
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const bool wasLong = longPress.mouseUp();
        const int scene = pressedScene;
        pressedScene = -1;
        repaint();

        if (dragging)
        {
            dragging = false;
            owner.endDrag (e.getScreenPosition());
            return;
        }
        if (scene < 0)
            return;

        if (wasLong)
        {
            auto& studio = owner.studio;
            juce::PopupMenu menu;
            menu.addSectionHeader ("Scene " + juce::String (scene + 1));
            menu.addItem (1, "Duplicate", scene < numScenes - 1 && sceneIsEmpty (studio.getSet(), numScenes - 1));
            menu.addItem (2, "Delete");
            rb::showMenu (menu, *this, [&studio, scene] (int result)
            {
                if (result == 1) studio.getDocument().edit ([scene] (Set& s) { duplicateScene (s, scene); });
                if (result == 2) studio.getDocument().edit ([scene] (Set& s) { deleteScene (s, scene); });
            });
        }
        else if (e.mouseWasClicked())
        {
            owner.studio.launchScene (scene); // SCN-2
        }
    }

    SessionView& owner;
    LongPress longPress;
    int gridTop = 0, rowH = 0, pressedScene = -1;
    bool dragging = false;
};

//==============================================================================
class SessionView::AddTrackColumn : public juce::Component
{
public:
    explicit AddTrackColumn (SessionView& o) : owner (o)
    {
        addAndMakeVisible (button);
        button.setFilled (true);
        button.onClick = [this] { showMenu(); };
    }

    void resized() override
    {
        button.setBounds (getLocalBounds().removeFromTop (headerHeight).reduced (6, 8));
    }

    void showMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Add Track");
        const auto& categories = getPresetCategories();
        for (int i = 0; i < categories.size(); ++i)
            menu.addItem (i + 1, categories[i]);

        auto& studio = owner.studio;
        auto& nav = owner.nav;
        rb::showMenu (menu, button, [&studio, &nav] (int result)
        {
            if (result <= 0)
                return;
            const auto category = getPresetCategories()[result - 1];
            std::vector<const TrackPreset*> matches;
            for (auto& p : getFactoryPresets())
                if (p.category == category)
                    matches.push_back (&p);
            if (matches.empty())
                return;

            const auto* preset = matches[(size_t) juce::Random::getSystemRandom().nextInt ((int) matches.size())];
            int newId = 0;
            studio.getDocument().edit ([&] (Set& s)
            {
                if (addTrack (s, *preset))
                    newId = s.tracks.back().id;
            });
            if (newId != 0)
                nav.showInstrument (newId, -1);
        });
    }

    SessionView& owner;
    IconButton button { Icon::plus };
};

//==============================================================================
SessionView::SessionView (Studio& s, Navigator& n) : studio (s), nav (n)
{
    addAndMakeVisible (header);
    header.onBack = [this] { nav.showLibrary(); };
    header.onTitleClicked = [this]
    {
        askForText ("Rename Set", studio.getSet().name, [this] (juce::String name)
        {
            if (name.isNotEmpty())
                studio.getDocument().edit ([name] (Set& set) { set.name = name; });
        });
    };
    header.addRightButton (settingsButton);
    settingsButton.onClick = [this] { nav.showSheet (std::make_unique<SetSettingsSheet> (studio)); };

    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&grid, false);
    viewport.setScrollBarsShown (false, false, true, true);

    sceneColumn = std::make_unique<SceneColumn> (*this);
    addAndMakeVisible (*sceneColumn);
    addColumn = std::make_unique<AddTrackColumn> (*this);
    grid.addAndMakeVisible (*addColumn);

    for (auto* b : { &undoButton, &redoButton, &mixerButton, &scenesButton, &playButton })
        addAndMakeVisible (b);

    undoButton.onClick = [this] { studio.getDocument().undo(); };
    redoButton.onClick = [this] { studio.getDocument().redo(); };
    mixerButton.onClick = [this]
    {
        showMixer = ! showMixer;
        mixerButton.setHighlight (showMixer);
        for (auto& c : columns)
            c->setMixerVisible (showMixer);
        layoutGrid();
    };
    scenesButton.onClick = [this]
    {
        showScenes = ! showScenes;
        scenesButton.setHighlight (showScenes);
        resized();
    };
    scenesButton.setHighlight (showScenes);
    playButton.setFilled (true);
    playButton.onClick = [this] { studio.togglePlay(); };

    studio.addListener (this);
    rebuild();
    startTimerHz (30);
}

SessionView::~SessionView()
{
    studio.removeListener (this);
}

void SessionView::rebuild()
{
    std::vector<int> ids;
    for (auto& t : studio.getSet().tracks)
        ids.push_back (t.id);

    if (ids != builtTrackIds)
    {
        columns.clear();
        for (auto id : ids)
        {
            columns.push_back (std::make_unique<TrackColumn> (*this, id));
            grid.addAndMakeVisible (*columns.back());
            columns.back()->setMixerVisible (showMixer);
        }
        builtTrackIds = ids;
        addColumn->setVisible ((int) ids.size() < maxTracks);
        layoutGrid();
    }

    header.setTitle (studio.getSet().name);
    for (auto& c : columns)
        c->update();
    undoButton.setEnabled (studio.getDocument().canUndo());
    redoButton.setEnabled (studio.getDocument().canRedo());
    sceneColumn->repaint();
}

void SessionView::studioChanged()
{
    if (! studio.isSetOpen())
        return;
    rebuild();
}

void SessionView::timerCallback()
{
    for (auto& c : columns)
        for (auto* child : c->getChildren())
            child->repaint();
    updateTransportButton();
}

void SessionView::updateTransportButton()
{
    const bool playing = studio.isPlaying();
    playButton.setIcon (playing ? Icon::stop : Icon::play);
    playButton.setIconColour (playing ? std::optional<juce::Colour> (Theme::get().playing) : std::nullopt);
}

void SessionView::paint (juce::Graphics& g)
{
    const auto& t = Theme::get();
    g.fillAll (t.background);
    g.setColour (t.surface);
    g.fillRect (getLocalBounds().removeFromBottom (Theme::barHeight + 8));

    if (drag.kind != DragState::Kind::none)
    {
        g.setColour (t.accent.withAlpha (0.8f));
        g.fillEllipse (juce::Rectangle<float> (28.0f, 28.0f).withCentre (drag.position.toFloat()));
    }
}

void SessionView::resized()
{
    auto bounds = getLocalBounds();
    header.setBounds (bounds.removeFromTop (Theme::barHeight));

    auto bottom = bounds.removeFromBottom (Theme::barHeight + 8).reduced (8, 4);
    const int w = Theme::touch + 8;
    undoButton.setBounds (bottom.removeFromLeft (w));
    redoButton.setBounds (bottom.removeFromLeft (w));
    playButton.setBounds (bottom.removeFromRight (64));
    bottom.removeFromRight (8);
    scenesButton.setBounds (bottom.removeFromRight (w));
    mixerButton.setBounds (bottom.removeFromRight (w));

    bounds.reduce (4, 0);
    sceneColumn->setVisible (showScenes);
    if (showScenes)
        sceneColumn->setBounds (bounds.removeFromLeft (sceneWidth));

    viewport.setBounds (bounds);
    layoutGrid();
}

void SessionView::layoutGrid()
{
    auto area = viewport.getLocalBounds();
    const int available = area.getHeight() - headerHeight - (showMixer ? mixerHeight : 0);
    const int rowHeight = juce::jmax (minRowHeight, available / numScenes);
    const int height = headerHeight + rowHeight * numScenes + (showMixer ? mixerHeight : 0);

    int x = 0;
    for (auto& c : columns)
    {
        c->setBounds (x, 0, columnWidth, height);
        x += columnWidth;
    }
    if (addColumn->isVisible())
    {
        addColumn->setBounds (x, 0, columnWidth, height);
        x += columnWidth;
    }

    grid.setSize (juce::jmax (x, area.getWidth()), height);
    sceneColumn->setGeometry (viewport.getY() - sceneColumn->getY() + headerHeight - viewport.getViewPositionY(), rowHeight);
}

//==============================================================================
void SessionView::beginDrag (DragState::Kind kind, int trackId, int slot, juce::Point<int> screenPos)
{
    drag.kind = kind;
    drag.trackId = trackId;
    drag.slot = slot;
    drag.position = getLocalPoint (nullptr, screenPos);
    repaint();
}

void SessionView::updateDrag (juce::Point<int> screenPos)
{
    drag.position = getLocalPoint (nullptr, screenPos);
    repaint();
}

void SessionView::endDrag (juce::Point<int> screenPos)
{
    const auto d = drag;
    drag.kind = DragState::Kind::none;
    repaint();

    const auto inGrid = grid.getLocalPoint (nullptr, screenPos);

    // Which column and slot is under the finger?
    int targetIndex = -1, targetSlot = -1, targetTrackId = 0;
    for (size_t i = 0; i < columns.size(); ++i)
    {
        auto b = columns[i]->getBounds();
        if (inGrid.x >= b.getX() && inGrid.x < b.getRight())
        {
            targetIndex = (int) i;
            targetTrackId = builtTrackIds[i];
            targetSlot = columns[i]->getSlotAt (inGrid - b.getPosition());
        }
    }

    switch (d.kind)
    {
        case DragState::Kind::clip:
            if (targetTrackId != 0 && targetSlot >= 0 && (targetTrackId != d.trackId || targetSlot != d.slot))
                studio.getDocument().edit ([=] (Set& s) { moveClip (s, d.trackId, d.slot, targetTrackId, targetSlot); });
            break;

        case DragState::Kind::track:
            if (targetIndex >= 0)
                studio.getDocument().edit ([=] (Set& s) { moveTrack (s, d.trackId, targetIndex); });
            break;

        case DragState::Kind::scene:
        {
            const int scene = sceneColumn->sceneAt (sceneColumn->getLocalPoint (nullptr, screenPos).y);
            if (scene >= 0 && scene != d.slot)
                studio.getDocument().edit ([=] (Set& s) { moveScene (s, d.slot, scene); });
            break;
        }

        case DragState::Kind::none:
            break;
    }
}

} // namespace rb
