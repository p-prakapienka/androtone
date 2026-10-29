namespace rb
{

//==============================================================================
// Preview pads for Key & Scale: play the last melodic track (SET-3).
class SetSettingsSheet::PreviewPads : public juce::Component
{
public:
    explicit PreviewPads (SetSettingsSheet& o) : owner (o) {}

    int noteForColumn (int column) const
    {
        auto& set = owner.studio.getSet();
        auto* track = set.findTrack (owner.previewTrackId());
        if (track == nullptr)
            return -1;
        const int base = melodicBaseNote (set, *track) + 12;
        if (! set.inKey)
            return base + column;
        return transposeInScale (base, column, true, set.root, set.scale);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = Theme::get();
        auto& set = owner.studio.getSet();
        for (int i = 0; i < 8; ++i)
        {
            auto cell = juce::Rectangle<float> (i * getWidth() / 8.0f, 0.0f, getWidth() / 8.0f, (float) getHeight()).reduced (2.0f);
            const int note = noteForColumn (i);
            const bool lit = held.count (note) > 0;
            g.setColour (lit ? t.accent : (set.hasMelodicTrack() ? t.surfaceRaised : t.surface));
            g.fillRoundedRectangle (cell, 6.0f);
            g.setColour (set.hasMelodicTrack() ? t.text : t.textDim);
            g.setFont (Theme::font (12.0f));
            if (note >= 0)
                g.drawText (noteName (note), cell, juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int column = juce::jlimit (0, 7, (int) (e.position.x / (getWidth() / 8.0f)));
        const int note = noteForColumn (column);
        const int id = owner.previewTrackId();
        if (note < 0 || id == 0)
            return;
        sources[e.source.getIndex()] = note;
        held.insert (note);
        owner.studio.noteOn (id, note, 0.8f);
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (auto it = sources.find (e.source.getIndex()); it != sources.end())
        {
            owner.studio.noteOff (owner.previewTrackId(), it->second);
            held.erase (it->second);
            sources.erase (it);
            repaint();
        }
    }

    SetSettingsSheet& owner;
    std::map<int, int> sources;
    std::set<int> held;
};

SetSettingsSheet::SetSettingsSheet (Studio& s) : Sheet ("Set Settings"), studio (s)
{
    heightFraction = 0.75f;

    addAndMakeVisible (tempoSlider);
    tempoSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    tempoSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    tempoSlider.setRange (40.0, 240.0, 1.0); // coarse slider; +/- and tap reach the full range
    tempoSlider.onDragStart = [this] { studio.getDocument().beginGesture(); };
    tempoSlider.onDragEnd = [this] { studio.getDocument().endGesture(); };
    tempoSlider.onValueChange = [this]
    {
        const double bpm = tempoSlider.getValue();
        if (std::abs (bpm - studio.getSet().tempo) >= 0.5)
            studio.getDocument().edit ([bpm] (Set& set) { set.tempo = bpm; });
    };

    auto nudge = [this] (double delta)
    {
        studio.getDocument().edit ([delta] (Set& set)
        {
            set.tempo = juce::jlimit (minTempo, maxTempo, std::round (set.tempo) + delta);
        });
    };
    addAndMakeVisible (tempoDown);
    addAndMakeVisible (tempoUp);
    tempoDown.onClick = [nudge] { nudge (-1.0); };
    tempoUp.onClick = [nudge] { nudge (1.0); };

    addAndMakeVisible (tapButton);
    tapButton.setTriggeredOnMouseDown (true);
    tapButton.onClick = [this] { studio.tapTempo(); };

    addAndMakeVisible (metronomeToggle);
    metronomeToggle.onClick = [this]
    {
        const bool on = metronomeToggle.getToggleState();
        studio.getDocument().edit ([on] (Set& set) { set.metronome = on; });
    };

    addAndMakeVisible (inKeyToggle);
    inKeyToggle.onClick = [this]
    {
        const bool on = inKeyToggle.getToggleState();
        studio.getDocument().edit ([on] (Set& set) { set.inKey = on; });
    };

    for (int pc = 0; pc < 12; ++pc)
    {
        auto button = std::make_unique<PillButton> (pitchClassName (pc));
        button->onClick = [this, pc] { studio.getDocument().edit ([pc] (Set& set) { set.root = pc; }); };
        addAndMakeVisible (*button);
        rootButtons.push_back (std::move (button));
    }

    addAndMakeVisible (scaleBox);
    for (size_t i = 0; i < getScales().size(); ++i)
        scaleBox.addItem (getScales()[i].name, (int) i + 1);
    scaleBox.onChange = [this]
    {
        const int scale = scaleBox.getSelectedId() - 1;
        if (scale >= 0 && scale != studio.getSet().scale)
            studio.getDocument().edit ([scale] (Set& set) { set.scale = scale; });
    };

    previewPads = std::make_unique<PreviewPads> (*this);
    addAndMakeVisible (*previewPads);

    addAndMakeVisible (exportButton);
    exportButton.onClick = [this]
    {
        if (auto* main = findParentComponentOfClass<MainComponent>())
            main->showSheet (std::make_unique<ExportSheet> (studio));
    };

    studio.addListener (this);
    update();
}

SetSettingsSheet::~SetSettingsSheet()
{
    studio.removeListener (this);
}

int SetSettingsSheet::previewTrackId() const
{
    // The last melodic track in the set.
    auto& tracks = studio.getSet().tracks;
    for (auto it = tracks.rbegin(); it != tracks.rend(); ++it)
        if (it->isMelodic())
            return it->id;
    return 0;
}

void SetSettingsSheet::studioChanged() { update(); }

void SetSettingsSheet::update()
{
    auto& set = studio.getSet();
    tempoSlider.setValue (set.tempo, juce::dontSendNotification);
    metronomeToggle.setToggleState (set.metronome, juce::dontSendNotification);
    inKeyToggle.setToggleState (set.inKey, juce::dontSendNotification);
    scaleBox.setSelectedId (set.scale + 1, juce::dontSendNotification);

    // SET-3: key options need a melodic track.
    const bool melodic = set.hasMelodicTrack();
    inKeyToggle.setEnabled (melodic);
    scaleBox.setEnabled (melodic && set.inKey);
    for (int pc = 0; pc < 12; ++pc)
    {
        rootButtons[(size_t) pc]->setEnabled (melodic && set.inKey);
        rootButtons[(size_t) pc]->setToggleState (set.root == pc, juce::dontSendNotification);
    }

    exportButton.setEnabled (set.hasAnyClip()); // SET-4
    previewPads->repaint();
    repaint();
}

void SetSettingsSheet::paint (juce::Graphics& g)
{
    Sheet::paint (g);
    const auto& t = Theme::get();

    g.setColour (t.text);
    g.setFont (Theme::font (34.0f, true));
    g.drawText (juce::String (studio.getSet().tempo, studio.getSet().tempo == std::round (studio.getSet().tempo) ? 0 : 1),
                tempoLabelArea, juce::Justification::centredLeft);
    g.setColour (t.textDim);
    g.setFont (Theme::font (14.0f));
    g.drawText ("BPM", tempoLabelArea.withTrimmedLeft (90), juce::Justification::centredLeft);

    g.setColour (t.textDim);
    g.setFont (Theme::font (13.0f, true));
    g.drawText ("KEY & SCALE", keyLabelArea, juce::Justification::bottomLeft);
}

void SetSettingsSheet::resized()
{
    Sheet::resized();
    auto area = getContentArea();

    auto tempoRow = area.removeFromTop (48);
    tapButton.setBounds (tempoRow.removeFromRight (80).reduced (2, 4));
    tempoLabelArea = tempoRow;

    auto sliderRow = area.removeFromTop (48);
    tempoDown.setBounds (sliderRow.removeFromLeft (Theme::touch));
    tempoUp.setBounds (sliderRow.removeFromRight (Theme::touch));
    tempoSlider.setBounds (sliderRow.reduced (6, 8));

    area.removeFromTop (8);
    metronomeToggle.setBounds (area.removeFromTop (Theme::touch));

    keyLabelArea = area.removeFromTop (32);
    inKeyToggle.setBounds (area.removeFromTop (Theme::touch));

    auto rootArea = area.removeFromTop (88);
    const int w = rootArea.getWidth() / 6;
    for (int pc = 0; pc < 12; ++pc)
        rootButtons[(size_t) pc]->setBounds (rootArea.getX() + (pc % 6) * w, rootArea.getY() + (pc / 6) * 44, w, 44);

    scaleBox.setBounds (area.removeFromTop (Theme::touch).reduced (0, 2));
    area.removeFromTop (8);
    previewPads->setBounds (area.removeFromTop (56));
    area.removeFromTop (12);
    exportButton.setBounds (area.removeFromTop (Theme::touch).withSizeKeepingCentre (220, Theme::touch));
}

//==============================================================================
AppSettingsSheet::AppSettingsSheet (Studio& s) : Sheet ("Settings"), studio (s)
{
    heightFraction = 0.85f;
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);

    auto& settings = studio.getSettings();

    themeBox.addItem ("Dark", 1);
    themeBox.addItem ("High Contrast", 2);
    themeBox.setSelectedId ((int) settings.getTheme() + 1, juce::dontSendNotification);
    themeBox.onChange = [this] { studio.getSettings().setTheme ((AppSettings::Theme) (themeBox.getSelectedId() - 1)); };

    layoutBox.addItem ("Fourths", 1);
    layoutBox.addItem ("Sequential", 2);
    layoutBox.setSelectedId ((int) settings.getMelodicLayout() + 1, juce::dontSendNotification);
    layoutBox.onChange = [this] { studio.getSettings().setMelodicLayout ((AppSettings::MelodicLayout) (layoutBox.getSelectedId() - 1)); };

    auto bindToggle = [&] (juce::ToggleButton& toggle, bool value, std::function<void (bool)> setter)
    {
        toggle.setToggleState (value, juce::dontSendNotification);
        toggle.onClick = [&toggle, setter] { setter (toggle.getToggleState()); };
        content.addAndMakeVisible (toggle);
    };

    bindToggle (captureTimeout, settings.getCaptureTimeout(), [this] (bool b) { studio.getSettings().setCaptureTimeout (b); });
    bindToggle (noteNames, settings.getShowNoteNames(), [this] (bool b) { studio.getSettings().setShowNoteNames (b); });
    bindToggle (sampleNames, settings.getShowSampleNames(), [this] (bool b) { studio.getSettings().setShowSampleNames (b); });
    bindToggle (showTouches, settings.getShowTouches(), [this] (bool b) { studio.getSettings().setShowTouches (b); });
    bindToggle (monitorInput, settings.getMonitorInput(), [this] (bool b) { studio.getSettings().setMonitorInput (b); });

    content.addAndMakeVisible (themeBox);
    content.addAndMakeVisible (layoutBox);
    content.addAndMakeVisible (themeLabel);
    content.addAndMakeVisible (layoutLabel);
    content.addAndMakeVisible (midiButton);
    midiButton.onClick = [this]
    {
        if (auto* main = findParentComponentOfClass<MainComponent>())
            main->showSheet (std::make_unique<MidiSettingsSheet> (studio));
    };

    versionLabel.setText ("Riffbook " + juce::JUCEApplication::getInstance()->getApplicationVersion()
                              + "\nBuilt with JUCE. Sounds generated in code.",
                          juce::dontSendNotification);
    versionLabel.setColour (juce::Label::textColourId, Theme::get().textDim);
    versionLabel.setJustificationType (juce::Justification::topLeft);
    content.addAndMakeVisible (versionLabel);
}

void AppSettingsSheet::paint (juce::Graphics& g)
{
    Sheet::paint (g);
}

void AppSettingsSheet::resized()
{
    Sheet::resized();
    auto area = getContentArea();
    viewport.setBounds (area);

    const int w = area.getWidth() - 8;
    int y = 0;
    auto row = [&] (int h) { auto r = juce::Rectangle<int> (0, y, w, h); y += h + 6; return r; };

    auto labelled = [&] (juce::Component& c, juce::Label& label)
    {
        auto r = row (Theme::touch);
        c.setBounds (r.withTrimmedLeft (w / 2));
        label.setBounds (r.withWidth (w / 2));
    };

    labelled (themeBox, themeLabel);
    labelled (layoutBox, layoutLabel);
    for (auto* t : { &captureTimeout, &noteNames, &sampleNames, &showTouches, &monitorInput })
        t->setBounds (row (Theme::touch));
    midiButton.setBounds (row (Theme::touch));
    versionLabel.setBounds (row (60));
    content.setSize (w, y);
}

//==============================================================================
ExportSheet::ExportSheet (Studio& s) : Sheet ("Export Audio"), juce::Thread ("Export"), studio (s)
{
    heightFraction = 0.5f;
    formats.registerBasicFormats();

    addAndMakeVisible (formatBox);
    formatBox.addItem ("WAV (48 kHz, 16-bit)", 1);
    formatBox.addItem ("WAV (48 kHz, 24-bit)", 2);
    formatBox.addItem ("FLAC (48 kHz, compressed)", 3);
    formatBox.setSelectedId (1, juce::dontSendNotification);

    addAndMakeVisible (exportButton);
    addChildComponent (previewButton);
    addChildComponent (shareButton);

    exportButton.onClick = [this] { startExport(); };
    previewButton.onClick = [this] { togglePreview(); };
    shareButton.onClick = [this]
    {
        if (result.existsAsFile())
            shareBox = juce::ContentSharer::shareFilesScoped ({ juce::URL (result) }, nullptr, this);
    };
}

ExportSheet::~ExportSheet()
{
    stopThread (5000);
    if (deviceManager != nullptr)
        deviceManager->removeAudioCallback (&player);
    player.setSource (nullptr);
    if (transport != nullptr)
        transport->setSource (nullptr);
}

void ExportSheet::startExport()
{
    if (isThreadRunning())
        return;

    studio.stop();
    studio.getDocument().saveNow();
    setCopy = studio.getSet();
    setFolder = studio.getDocument().getFolder();
    format = formatBox.getSelectedId() == 2 ? OfflineRenderer::Format::wav24
           : formatBox.getSelectedId() == 3 ? OfflineRenderer::Format::flac
                                            : OfflineRenderer::Format::wav16;
    progress = 0.0f;
    finished = false;
    failed = false;
    exportButton.setEnabled (false);
    formatBox.setEnabled (false);
    startThread();
    repaint();
}

void ExportSheet::run()
{
    // Rendering uses its own processors, so the live engine is not disturbed.
    SampleLibrary library;
    library.setUserFolder (studio.getUserSamplesFolder());

    auto audio = OfflineRenderer::renderScenes (setCopy, setFolder, library, 48000.0, 2.0, [this] (float p)
    {
        progress = p;
        triggerAsyncUpdate();
        return ! threadShouldExit();
    });

    if (audio.getNumSamples() > 0 && ! threadShouldExit())
    {
        // Normalise down only if the mix clips.
        const float peak = juce::jmax (audio.getMagnitude (0, 0, audio.getNumSamples()),
                                       audio.getMagnitude (1, 0, audio.getNumSamples()));
        if (peak > 0.98f)
            audio.applyGain (0.98f / peak);

        const auto extension = format == OfflineRenderer::Format::flac ? ".flac" : ".wav";
        result = juce::File::getSpecialLocation (juce::File::tempDirectory)
                     .getChildFile (juce::File::createLegalFileName (setCopy.name) + extension);
        failed = ! OfflineRenderer::write (audio, 48000.0, format, result);
    }
    else
    {
        failed = true;
    }

    finished = true;
    triggerAsyncUpdate();
}

void ExportSheet::handleAsyncUpdate()
{
    if (finished)
    {
        exportButton.setVisible (false);
        formatBox.setVisible (false);
        previewButton.setVisible (! failed);
        shareButton.setVisible (! failed);
        resized();
    }
    repaint();
}

void ExportSheet::togglePreview()
{
    if (transport != nullptr && transport->isPlaying())
    {
        transport->stop();
        previewButton.setButtonText ("Play Preview");
        return;
    }

    if (transport == nullptr)
    {
        auto* reader = formats.createReaderFor (result);
        if (reader == nullptr)
            return;
        readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
        transport = std::make_unique<juce::AudioTransportSource>();
        transport->setSource (readerSource.get(), 0, nullptr, reader->sampleRate);
        player.setSource (transport.get());
        deviceManager = &studio.getEngine().getDeviceManager();
        deviceManager->addAudioCallback (&player);
    }

    transport->setPosition (0.0);
    transport->start();
    previewButton.setButtonText ("Stop Preview");
}

void ExportSheet::paint (juce::Graphics& g)
{
    Sheet::paint (g);
    const auto& t = Theme::get();
    auto area = getContentArea();
    area.removeFromTop (110);

    if (isThreadRunning() || (finished && ! failed))
    {
        auto bar = area.removeFromTop (12).toFloat();
        g.setColour (t.surfaceRaised);
        g.fillRoundedRectangle (bar, 6.0f);
        g.setColour (t.accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * progress.load()), 6.0f);
    }

    g.setColour (failed ? t.danger : t.textDim);
    g.setFont (Theme::font (14.0f));
    juce::String text = "All scenes are rendered in order; each lasts as long as its longest clip.";
    if (isThreadRunning()) text = "Rendering...";
    if (finished && ! failed) text = "Done: " + result.getFileName();
    if (failed) text = "Export failed.";
    g.drawFittedText (text, area.removeFromTop (60), juce::Justification::centredLeft, 3);
}

void ExportSheet::resized()
{
    Sheet::resized();
    auto area = getContentArea();
    formatBox.setBounds (area.removeFromTop (Theme::touch));
    area.removeFromTop (12);
    auto buttons = area.removeFromTop (Theme::touch);
    exportButton.setBounds (buttons.withSizeKeepingCentre (200, Theme::touch));
    previewButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (4, 0));
    shareButton.setBounds (buttons.reduced (4, 0));
}

} // namespace rb
