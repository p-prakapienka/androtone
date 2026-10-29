namespace rb
{

double InstrumentView::quantizeGrid = 0.25;
float InstrumentView::quantizeStrength = 1.0f;
bool InstrumentView::quantizeTriplets = false;

//==============================================================================
// Capture / Create Clip / Add, with a clear button (CAP-2..CAP-6).
class InstrumentView::CaptureStrip : public juce::Component
{
public:
    explicit CaptureStrip (InstrumentView& o) : owner (o)
    {
        addAndMakeVisible (mainButton);
        addAndMakeVisible (clearButton);
        mainButton.onClick = [this] { activate(); };
        clearButton.onClick = [this] { clear(); };
        update();
    }

    enum class State { idle, capture, createClip, add };

    State currentState() const
    {
        auto& studio = owner.studio;
        const int track = owner.trackId;
        const int playing = studio.getPlayingSlot (track);

        if (playing >= 0 && (studio.hasPendingNotes (track, playing) || ! studio.getPendingAutomation (track).empty()))
            return State::add;
        if (studio.canCaptureFree (track))
            return studio.isPlaying() ? State::createClip : State::capture;
        return State::idle;
    }

    void update()
    {
        const auto s = currentState();
        if (s != state)
        {
            state = s;
            switch (state)
            {
                case State::capture:    mainButton.setButtonText ("Capture");     mainButton.setStyle (PillButton::Style::accent); break;
                case State::createClip: mainButton.setButtonText ("Create Clip"); mainButton.setStyle (PillButton::Style::accent); break;
                case State::add:        mainButton.setButtonText ("Add");         mainButton.setStyle (PillButton::Style::accent); break;
                case State::idle:       mainButton.setButtonText ("Capture");     mainButton.setStyle (PillButton::Style::subtle); break;
            }
            mainButton.setEnabled (state != State::idle);
            clearButton.setVisible (state == State::capture || state == State::createClip);
            resized();
        }
        repaint();
    }

    void activate()
    {
        auto& studio = owner.studio;
        const int track = owner.trackId;

        switch (state)
        {
            case State::capture:
            case State::createClip:
            {
                const int preferred = owner.editSlot >= 0 && studio.getSet().findTrack (track) != nullptr
                                          && studio.getSet().findTrack (track)->slots[(size_t) owner.editSlot] == nullptr
                                          ? owner.editSlot : -1;
                const int slot = studio.captureFree (track, preferred);
                if (slot >= 0)
                {
                    owner.editSlot = slot;
                    owner.clipEditor.setSlot (slot);
                }
                break;
            }
            case State::add:
                studio.addPending (track, studio.getPlayingSlot (track));
                break;
            case State::idle:
                break;
        }
        update();
    }

    void clear()
    {
        owner.studio.clearCapture (owner.trackId);
        update();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = Theme::get();
        auto& studio = owner.studio;
        const int track = owner.trackId;

        juce::String status;
        if (state == State::add)
        {
            const int count = (int) studio.getPendingNotes (track, studio.getPlayingSlot (track)).size();
            status = count > 0 ? juce::String (count) + " new notes" : "New automation";
        }
        else if (state == State::capture || state == State::createClip)
        {
            int count = 0;
            for (auto& n : studio.getCapture().getNotes (track))
                count += n.slot < 0 ? 1 : 0;
            status = juce::String (count) + (count == 1 ? " note played" : " notes played");
        }
        else if (studio.getPlayingSlot (track) >= 0)
        {
            status = "Play to overdub";
        }
        else
        {
            status = "Play the pads to capture";
        }

        g.setColour (t.textDim);
        g.setFont (Theme::font (13.0f));
        g.drawText (status, getLocalBounds().reduced (12, 0).withTrimmedRight (mainButton.getWidth() + 56),
                    juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (8, 4);
        clearButton.setBounds (bounds.removeFromRight (clearButton.isVisible() ? Theme::touch : 0));
        mainButton.setBounds (bounds.removeFromRight (130));
    }

    InstrumentView& owner;
    PillButton mainButton { "Capture", PillButton::Style::subtle };
    IconButton clearButton { Icon::close };
    State state = State::add; // forces the first update
};

//==============================================================================
// Layout, octave and repeat (PAD-2, PAD-4, PAD-6, PAD-7). Stays usable while pads are
// held, so a rate can be tapped with one hand while holding a pad with the other.
class InstrumentView::PerformanceStrip : public juce::Component
{
public:
    explicit PerformanceStrip (InstrumentView& o) : owner (o)
    {
        addAndMakeVisible (layoutButton);
        addAndMakeVisible (octaveDown);
        addAndMakeVisible (octaveUp);

        layoutButton.onClick = [this]
        {
            const int id = owner.trackId;
            owner.studio.getDocument().editWithoutUndo ([id] (Set& s)
            {
                if (auto* t = s.findTrack (id))
                    t->perf.layout = t->perf.layout == PerformanceOptions::pads ? PerformanceOptions::alternate : PerformanceOptions::pads;
            });
        };
        octaveDown.onClick = [this] { shiftOctave (-1); };
        octaveUp.onClick = [this] { shiftOctave (1); };

        const auto& rates = getRepeatRates();
        for (int i = 0; i < (int) rates.size(); ++i)
        {
            auto button = std::make_unique<PillButton> (rates[(size_t) i].label, PillButton::Style::normal);
            button->onClick = [this, i]
            {
                const int id = owner.trackId;
                owner.studio.getDocument().editWithoutUndo ([id, i] (Set& s) { if (auto* t = s.findTrack (id)) t->perf.repeat = i; });
            };
            addAndMakeVisible (*button);
            rateButtons.push_back (std::move (button));
        }
        update();
    }

    void shiftOctave (int delta)
    {
        const int id = owner.trackId;
        owner.studio.getDocument().editWithoutUndo ([id, delta] (Set& s)
        {
            if (auto* t = s.findTrack (id))
                t->perf.octave = juce::jlimit (-3, 3, t->perf.octave + delta);
        });
    }

    void update()
    {
        auto* track = owner.studio.getSet().findTrack (owner.trackId);
        if (track == nullptr)
            return;

        const bool drum = track->isDrum();
        const bool alternate = track->perf.layout == PerformanceOptions::alternate;
        layoutButton.setIcon (drum ? (alternate ? Icon::pads : Icon::single) : (alternate ? Icon::pads : Icon::keys));
        layoutButton.setLabel (drum ? (alternate ? "Pads" : "Single") : (alternate ? "Pads" : "Keys"));
        octaveDown.setVisible (! drum);
        octaveUp.setVisible (! drum);

        for (size_t i = 0; i < rateButtons.size(); ++i)
            rateButtons[i]->setToggleState ((int) i == track->perf.repeat, juce::dontSendNotification);
        resized();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Theme::get().surface);
        g.fillRect (getLocalBounds());

        auto* track = owner.studio.getSet().findTrack (owner.trackId);
        if (track != nullptr && track->isMelodic())
        {
            g.setColour (Theme::get().textDim);
            g.setFont (Theme::font (12.0f));
            g.drawText ("Oct " + juce::String (track->perf.octave >= 0 ? "+" : "") + juce::String (track->perf.octave),
                        octaveLabelArea, juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (4, 4);
        layoutButton.setBounds (bounds.removeFromLeft (56));
        if (octaveDown.isVisible())
        {
            octaveDown.setBounds (bounds.removeFromLeft (36));
            octaveLabelArea = bounds.removeFromLeft (44);
            octaveUp.setBounds (bounds.removeFromLeft (36));
        }
        bounds.removeFromLeft (4);

        // Repeat rates: two rows when space is short.
        const int count = (int) rateButtons.size();
        const int perRow = bounds.getWidth() / count >= 40 ? count : (count + 1) / 2;
        const int rowsNeeded = (count + perRow - 1) / perRow;
        const int h = bounds.getHeight() / rowsNeeded;
        const int w = bounds.getWidth() / perRow;
        for (int i = 0; i < count; ++i)
            rateButtons[(size_t) i]->setBounds (bounds.getX() + (i % perRow) * w, bounds.getY() + (i / perRow) * h, w, h);
    }

    InstrumentView& owner;
    IconButton layoutButton { Icon::single, "Single" };
    IconButton octaveDown { Icon::chevronDown }, octaveUp { Icon::chevronUp };
    juce::Rectangle<int> octaveLabelArea;
    std::vector<std::unique_ptr<PillButton>> rateButtons;
};

//==============================================================================
InstrumentView::InstrumentView (Studio& s, Navigator& n, int id, int slot)
    : studio (s), nav (n), trackId (id), editSlot (slot), followPlaying (slot < 0),
      devicePanel (s, n, id), clipEditor (s, n, id)
{
    addAndMakeVisible (header);
    header.onBack = [this] { nav.showSession(); };
    header.onTitleClicked = [this] { devicePanel.browse (DevicePanel::Tab::instrument); };

    addAndMakeVisible (devicePanel);
    addAndMakeVisible (clipEditor);
    clipEditor.onExpandedChanged = [this] (bool expanded)
    {
        devicePanel.setCollapsed (expanded);
        resized();
    };
    devicePanel.onTabChanged = [this] { if (clipEditor.isExpanded()) clipEditor.setExpanded (false); };

    captureStrip = std::make_unique<CaptureStrip> (*this);
    addAndMakeVisible (*captureStrip);
    performanceStrip = std::make_unique<PerformanceStrip> (*this);
    addChildComponent (*performanceStrip);

    for (auto* b : { &undoButton, &redoButton, &perfButton, &duplicateButton, &quantizeButton, &playButton })
        addAndMakeVisible (b);

    undoButton.onClick = [this] { studio.getDocument().undo(); };
    redoButton.onClick = [this] { studio.getDocument().redo(); };
    perfButton.onClick = [this]
    {
        showPerformance = ! showPerformance;
        perfButton.setHighlight (showPerformance);
        performanceStrip->setVisible (showPerformance);
        resized();
    };
    duplicateButton.onClick = [this] { duplicateClip(); };
    playButton.setFilled (true);
    playButton.onClick = [this] { studio.togglePlay(); };

    // Quantize: tap quantizes the clip; long press opens the settings (INS-5).
    quantizeButton.onClick = [this]
    {
        if (! quantizeLongPress.hasFired())
            quantizeAll();
    };
    quantizeLongPress.onLongPress = [this] (juce::Point<int>) { showQuantizeMenu(); };
    quantizeButton.addMouseListener (this, false);

    if (followPlaying)
        updateSlotFollow();
    clipEditor.setSlot (editSlot);

    studio.addListener (this);
    refreshPads();
    updateHeader();
    startTimerHz (20);
}

InstrumentView::~InstrumentView()
{
    studio.removeListener (this);
    studio.allNotesOff (trackId);
}

void InstrumentView::refreshPads()
{
    auto* track = studio.getSet().findTrack (trackId);
    if (track == nullptr)
        return;

    const bool alternate = track->perf.layout == PerformanceOptions::alternate;
    const juce::String kind = track->isDrum() ? (alternate ? "single" : "drum") : (alternate ? "keys" : "melodic");

    if (kind != padKind)
    {
        padKind = kind;
        if (kind == "drum")         pads = std::make_unique<DrumPadGrid> (studio, trackId);
        else if (kind == "single")  pads = std::make_unique<SinglePad> (studio, trackId);
        else if (kind == "keys")    pads = std::make_unique<KeysView> (studio, trackId);
        else                        pads = std::make_unique<MelodicPadGrid> (studio, trackId);

        if (track->isDrum())
        {
            pads->onNoteTouched = [this] (int note)
            {
                const int pad = note - drumBaseNote;
                auto* t = studio.getSet().findTrack (trackId);
                if (t != nullptr && pad >= 0 && pad < numDrumPads && t->perf.selectedPad != pad)
                {
                    const int id = trackId;
                    studio.getDocument().editWithoutUndo ([id, pad] (Set& s) { if (auto* tr = s.findTrack (id)) tr->perf.selectedPad = pad; });
                }
            };
        }

        addAndMakeVisible (*pads);
        resized();
    }
    else if (pads != nullptr)
    {
        pads->repaint();
    }
}

void InstrumentView::updateHeader()
{
    if (auto* track = studio.getSet().findTrack (trackId))
        header.setTitle (track->presetName.isNotEmpty() ? track->presetName : track->name, track->presetModified);
    undoButton.setEnabled (studio.getDocument().canUndo());
    redoButton.setEnabled (studio.getDocument().canRedo());

    auto* track = studio.getSet().findTrack (trackId);
    duplicateButton.setEnabled (track != nullptr && editSlot >= 0 && track->slots[(size_t) editSlot] != nullptr);
    quantizeButton.setEnabled (duplicateButton.isEnabled());
}

void InstrumentView::updateSlotFollow()
{
    auto* track = studio.getSet().findTrack (trackId);
    if (track == nullptr)
        return;

    int slot = studio.getPlayingSlot (trackId);
    if (slot < 0 && (editSlot < 0 || track->slots[(size_t) editSlot] == nullptr))
        for (int s = 0; s < numScenes && slot < 0; ++s)
            if (track->slots[(size_t) s] != nullptr)
                slot = s;

    if (slot >= 0 && slot != editSlot)
    {
        editSlot = slot;
        clipEditor.setSlot (slot);
        updateHeader();
    }
}

void InstrumentView::studioChanged()
{
    if (studio.getSet().findTrack (trackId) == nullptr)
    {
        // The track was deleted (e.g. by undo); go back to the grid.
        juce::MessageManager::callAsync ([&nav = nav] { nav.showSession(); });
        return;
    }

    refreshPads();
    updateHeader();
    performanceStrip->update();
    captureStrip->update();
    repaint();
}

void InstrumentView::captureChanged()
{
    captureStrip->update();
}

void InstrumentView::timerCallback()
{
    if (followPlaying)
        updateSlotFollow();

    const bool playing = studio.isPlaying();
    playButton.setIcon (playing ? Icon::stop : Icon::play);
    playButton.setIconColour (playing ? std::optional<juce::Colour> (Theme::get().playing) : std::nullopt);
    captureStrip->update();
}

void InstrumentView::duplicateClip()
{
    if (editSlot < 0)
        return;

    int newSlot = -1;
    const int id = trackId, slot = editSlot;
    studio.getDocument().edit ([&] (Set& s) { newSlot = rb::duplicateClip (s, id, slot); });

    if (newSlot >= 0)
    {
        if (studio.getPlayingSlot (trackId) == slot)
            studio.switchClipSeamless (trackId, newSlot); // INS-4
        editSlot = newSlot;
        clipEditor.setSlot (newSlot);
    }
}

void InstrumentView::quantizeAll()
{
    auto* track = studio.getSet().findTrack (trackId);
    if (track == nullptr || editSlot < 0 || track->slots[(size_t) editSlot] == nullptr)
        return;

    const double grid = quantizeTriplets ? tripletOf (quantizeGrid) : quantizeGrid;
    const float strength = quantizeStrength;
    const int id = trackId, slot = editSlot;
    studio.getDocument().edit ([=] (Set& s)
    {
        if (auto* t = s.findTrack (id))
            if (auto& clip = t->slots[(size_t) slot])
                clip = quantizeNotes (*clip, selectAll (*clip), grid, strength).clip;
    });
}

void InstrumentView::showQuantizeMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Quantize to");
    int id = 1;
    for (auto& d : getGridDivisions())
        menu.addItem (id++, d.label, true, ! quantizeTriplets && quantizeGrid == d.beats);
    menu.addItem (100, "Triplets", true, quantizeTriplets);
    menu.addSeparator();
    menu.addItem (200, "Strength 100%", true, quantizeStrength >= 0.99f);
    menu.addItem (201, "Strength 75%", true, std::abs (quantizeStrength - 0.75f) < 0.01f);
    menu.addItem (202, "Strength 50%", true, std::abs (quantizeStrength - 0.5f) < 0.01f);

    juce::Component::SafePointer<InstrumentView> safe (this);
    showMenu (menu, quantizeButton, [safe] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        const auto& divisions = getGridDivisions();
        if (result >= 1 && result <= (int) divisions.size())
            quantizeGrid = divisions[(size_t) result - 1].beats;
        else if (result == 100)
            quantizeTriplets = ! quantizeTriplets;
        else if (result >= 200)
            quantizeStrength = result == 200 ? 1.0f : (result == 201 ? 0.75f : 0.5f);
    });
}

void InstrumentView::mouseDown (const juce::MouseEvent& e)
{
    if (e.eventComponent == &quantizeButton)
        quantizeLongPress.mouseDown (e);
}

void InstrumentView::mouseDrag (const juce::MouseEvent& e)
{
    if (e.eventComponent == &quantizeButton)
        quantizeLongPress.mouseDrag (e);
}

void InstrumentView::mouseUp (const juce::MouseEvent& e)
{
    if (e.eventComponent == &quantizeButton)
        quantizeLongPress.mouseUp();
}

void InstrumentView::paint (juce::Graphics& g)
{
    const auto& t = Theme::get();
    g.fillAll (t.background);
    g.setColour (t.surface);
    g.fillRect (getLocalBounds().removeFromBottom (Theme::barHeight + 8));
}

void InstrumentView::resized()
{
    auto bounds = getLocalBounds();
    header.setBounds (bounds.removeFromTop (Theme::barHeight));

    auto bottom = bounds.removeFromBottom (Theme::barHeight + 8).reduced (8, 4);
    const int w = Theme::touch + 6;
    undoButton.setBounds (bottom.removeFromLeft (w));
    redoButton.setBounds (bottom.removeFromLeft (w));
    playButton.setBounds (bottom.removeFromRight (64));
    bottom.removeFromRight (6);
    quantizeButton.setBounds (bottom.removeFromRight (w));
    duplicateButton.setBounds (bottom.removeFromRight (w));
    perfButton.setBounds (bottom.removeFromRight (w));

    const bool expanded = clipEditor.isExpanded();
    const bool landscape = bounds.getWidth() > bounds.getHeight() * 1.15f;

    if (expanded)
    {
        // Full-screen editor: the device section shrinks to its tabs and the pads hide (EDIT-2).
        devicePanel.setBounds (bounds.removeFromTop (40));
        if (pads != nullptr) pads->setVisible (false);
        performanceStrip->setVisible (false);
        captureStrip->setBounds (bounds.removeFromBottom (44));
        clipEditor.setBounds (bounds);
        return;
    }

    if (pads != nullptr) pads->setVisible (true);
    performanceStrip->setVisible (showPerformance);

    if (landscape)
    {
        auto right = bounds.removeFromRight (bounds.getWidth() / 2).reduced (4, 4);
        if (showPerformance)
            performanceStrip->setBounds (right.removeFromTop (96));
        if (pads != nullptr)
            pads->setBounds (right);

        captureStrip->setBounds (bounds.removeFromBottom (44));
        clipEditor.setBounds (bounds.removeFromBottom (juce::jmax (90, bounds.getHeight() / 3)));
        devicePanel.setBounds (bounds.reduced (4, 0));
        return;
    }

    devicePanel.setBounds (bounds.removeFromTop (juce::jmin (238, bounds.getHeight() / 3)).reduced (4, 0));
    clipEditor.setBounds (bounds.removeFromTop (juce::jmin (110, bounds.getHeight() / 5)));
    captureStrip->setBounds (bounds.removeFromTop (44));
    if (showPerformance)
        performanceStrip->setBounds (bounds.removeFromTop (getWidth() < 600 ? 92 : 50));

    if (pads != nullptr)
    {
        // Keep drum and melodic pads roughly square.
        auto padArea = bounds.reduced (6, 4);
        if (padKind == "drum" || padKind == "melodic")
        {
            const int size = juce::jmin (padArea.getWidth(), padArea.getHeight());
            padArea = padArea.withSizeKeepingCentre (padArea.getWidth(), size);
        }
        pads->setBounds (padArea);
    }
}

} // namespace rb
