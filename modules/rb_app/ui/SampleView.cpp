namespace rb
{

namespace
{
    constexpr double sampleViewMaxRecordSeconds = 30.0;
    constexpr double sampleViewMaxZoom = 64.0;

    double sampleViewNowMs() { return juce::Time::getMillisecondCounterHiRes(); }

    juce::String sampleViewFormatSeconds (double seconds)
    {
        const int minutes = (int) (seconds / 60.0);
        return juce::String (minutes) + ":" + juce::String (seconds - minutes * 60.0, 1).paddedLeft ('0', 4);
    }
}

SampleView::Source SampleView::lastSource = SampleView::Source::microphone;

SampleView::SampleView (Studio& s, Navigator& n, int id) : studio (s), nav (n), trackId (id)
{
    source = lastSource;
    shownRef = currentRef();

    loadButton.onClick = [this] { loadSample(); };
    optionsButton.onClick = [this] { showOptions(); };

    recordButton.setFilled (true);
    recordButton.onClick = [this]
    {
        if (recordLongPress.longPress.hasFired())
            return;
        toggleRecording();
    };

    // SMP-5: long-press switches between the microphone and the app's own output.
    recordLongPress.longPress.onLongPress = [this] (juce::Point<int>)
    {
        if (recording || waitingForPermission)
            return;
        source = source == Source::microphone ? Source::resample : Source::microphone;
        lastSource = source;
        flash (source == Source::microphone ? "Records the microphone" : "Records the app's output (resample)");
        updateRecordButton();
    };
    recordButton.addMouseListener (&recordLongPress, false);

    loadButton.setFilled (true);
    optionsButton.setFilled (true);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (recordButton);
    addAndMakeVisible (optionsButton);

    updateRecordButton();
    studio.addListener (this);
}

SampleView::~SampleView()
{
    studio.removeListener (this);
    recordButton.removeMouseListener (&recordLongPress);

    if (gestureOpen)
        studio.getDocument().endGesture();

    // Leaving the tab while recording keeps the take (unless the set is closing).
    if (recording)
    {
        if (studio.getDocument().isOpen())
        {
            finishTake();
        }
        else
        {
            studio.getSession().stopRecording();
            studio.getSession().setMonitorInput (false);
        }
    }
}

//==============================================================================
const Track* SampleView::track() const
{
    return studio.getSet().findTrack (trackId);
}

int SampleView::targetPad() const
{
    auto* t = track();
    return t != nullptr && t->isDrum() ? t->perf.selectedPad : -1;
}

juce::String SampleView::currentRef() const
{
    auto* t = track();
    if (t == nullptr)
        return {};
    return t->isDrum() ? t->pads[(size_t) t->perf.selectedPad].sampleRef : t->samplerSampleRef;
}

SamplePtr SampleView::currentSample()
{
    return studio.getDocument().getSample (currentRef());
}

ParamTarget SampleView::paramTarget (const juce::String& id) const
{
    ParamTarget target;
    target.paramIndex = -1;
    auto* t = track();
    if (t == nullptr)
        return target;

    if (t->isDrum())
    {
        target.device = ParamTarget::pad;
        target.padIndex = t->perf.selectedPad;
        target.paramIndex = getPadSpec().indexOf (id);
    }
    else
    {
        target.device = ParamTarget::instrument;
        target.paramIndex = getInstrumentSpec (t->instrumentType).indexOf (id);
    }
    return target;
}

float SampleView::paramValue (const juce::String& id, float fallback) const
{
    const auto target = paramTarget (id);
    if (target.paramIndex < 0)
        return fallback;
    return ParamKnob::valueFor (studio.getSet(), trackId, target);
}

void SampleView::setParam (const juce::String& id, float value, bool recordAutomation)
{
    const auto target = paramTarget (id);
    if (target.paramIndex < 0)
        return;
    const int tid = trackId;
    studio.getDocument().edit ([tid, target, value] (Set& s) { ParamKnob::setValue (s, tid, target, value); });
    if (recordAutomation)
        studio.recordParameterChange (trackId, target, value);
}

//==============================================================================
void SampleView::studioChanged()
{
    if (handlingChange)
        return;

    const juce::ScopedValueSetter<bool> guard (handlingChange, true);

    // SMP-3: tapping another pad while recording finishes this take and records into that pad.
    if (recording && recordPad >= 0 && targetPad() >= 0 && targetPad() != recordPad)
    {
        finishTake();
        beginTake();
    }

    if (currentRef() != shownRef)
    {
        shownRef = currentRef();
        zoom = 1.0;
        viewStart = 0.0;
    }

    repaint();
}

void SampleView::timerCallback()
{
    if (recording)
    {
        auto& session = studio.getSession();

        // Reached the maximum length: keep what was recorded.
        if (! session.isRecording())
        {
            finishTake();
            return;
        }

        // SMP-3: tapping the red (recording) pad stops, for microphone takes.
        if (recordPad >= 0 && source == Source::microphone)
        {
            const bool sounding = studio.isNoteSounding (trackId, drumBaseNote + recordPad);
            if (sounding && ! recordPadWasSounding && sampleViewNowMs() - recordStartMs > 400.0)
            {
                finishTake();
                return;
            }
            recordPadWasSounding = sounding;
        }
    }
    else if (flashMessage.isEmpty() || sampleViewNowMs() > flashUntilMs)
    {
        flashMessage = {};
        stopTimer();
    }

    repaint();
}

void SampleView::flash (const juce::String& message)
{
    flashMessage = message;
    flashUntilMs = sampleViewNowMs() + 2200.0;
    if (! isTimerRunning())
        startTimerHz (10);
    repaint();
}

//==============================================================================
void SampleView::updateRecordButton()
{
    if (recording || waitingForPermission)
    {
        recordButton.setIcon (Icon::stop);
        recordButton.setLabel ("Stop");
        recordButton.setIconColour (Theme::get().record);
        recordButton.setHighlight (false);
    }
    else
    {
        recordButton.setIcon (source == Source::microphone ? Icon::mic : Icon::record);
        recordButton.setLabel (source == Source::microphone ? "Mic" : "Resample");
        recordButton.setIconColour (Theme::get().record);
        recordButton.setHighlight (false);
    }
    repaint();
}

void SampleView::toggleRecording()
{
    if (recording)
    {
        finishTake();
        return;
    }

    if (waitingForPermission || track() == nullptr)
        return;

    if (studio.getSession().isRecording())
    {
        flash ("Another recording is running");
        return;
    }

    if (source == Source::resample)
    {
        beginTake();
        return;
    }

    // GLB-3: ask for the microphone the first time it is needed, then open the input.
    waitingForPermission = true;
    updateRecordButton();
    juce::Component::SafePointer<SampleView> safe (this);
    studio.getEngine().enableInput ([safe] (bool granted)
    {
        if (safe == nullptr)
            return;
        safe->waitingForPermission = false;
        if (granted)
            safe->beginTake();
        else
        {
            safe->updateRecordButton();
            safe->flash ("Microphone access is needed to record");
        }
    });
}

void SampleView::beginTake()
{
    auto& session = studio.getSession();
    if (session.isRecording() || track() == nullptr)
        return;

    recordPad = targetPad();
    recordPadWasSounding = recordPad >= 0 && studio.isNoteSounding (trackId, drumBaseNote + recordPad);

    // SMP-8: the input is audible while recording when monitoring is on.
    session.setMonitorInput (source == Source::microphone && studio.getSettings().getMonitorInput());
    session.startRecording (source == Source::microphone ? SessionProcessor::RecordSource::input
                                                         : SessionProcessor::RecordSource::output,
                            sampleViewMaxRecordSeconds);

    recording = true;
    recordStartMs = sampleViewNowMs();
    flashMessage = {};
    startTimerHz (30);
    updateRecordButton();
}

void SampleView::finishTake()
{
    if (! recording)
        return;

    auto& session = studio.getSession();
    auto raw = session.stopRecording();
    session.setMonitorInput (false);
    recording = false;
    stopTimer();

    const int pad = recordPad;
    recordPad = -1;
    updateRecordButton();

    auto& doc = studio.getDocument();
    const auto setFolder = doc.getFolder();
    const auto samplesFolder = setFolder.getChildFile ("samples");
    const auto file = setFolder == juce::File() ? juce::File() : makeRecordingFile (samplesFolder, juce::Time::getCurrentTime());
    const auto fileName = file != juce::File() ? file.getFileName()
                                               : "rec-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".wav";

    auto result = cleanUpRecording (raw, session.getSampleRate(), fileName.upToLastOccurrenceOf (".", false, false));
    if (result.sample == nullptr)
    {
        flash ("Nothing was recorded");
        return;
    }

    // SMP-3/SMP-4: store in the set, register it so it plays instantly, assign it.
    if (file != juce::File())
    {
        samplesFolder.createDirectory();
        if (! SampleLibrary::writeWav (*result.sample, file))
            flash ("The recording could not be saved");
    }

    const auto ref = "set:" + fileName;
    studio.getEngine().getSamples().put (ref, setFolder, result.sample);

    const int count = listSetSamples (setFolder).size();
    const auto padName = "Rec " + juce::String (juce::jmax (1, count));
    const int tid = trackId;

    doc.edit ([tid, pad, ref, padName] (Set& s)
    {
        auto* t = s.findTrack (tid);
        if (t == nullptr)
            return;

        if (pad >= 0 && t->isDrum())
        {
            t->pads[(size_t) pad].sampleRef = ref;
            t->pads[(size_t) pad].name = padName;
            if (const int index = getPadSpec().indexOf ("sampleStart"); index >= 0)
                ParamKnob::setValue (s, tid, { ParamTarget::pad, pad, index }, 0.0f);
        }
        else if (! t->isDrum())
        {
            t->samplerSampleRef = ref;
            if (const int index = getInstrumentSpec (t->instrumentType).indexOf ("sampleStart"); index >= 0)
                ParamKnob::setValue (s, tid, { ParamTarget::instrument, 0, index }, 0.0f);
        }
        t->presetModified = true;
    });
}

//==============================================================================
void SampleView::loadSample()
{
    nav.showSheet (std::make_unique<BrowserSheet> (studio, trackId, BrowserSheet::Mode::sample));
}

void SampleView::saveSampleToLibrary()
{
    const auto ref = currentRef();
    if (! isSetRecording (ref))
        return;

    const auto fileName = ref.fromFirstOccurrenceOf (":", false, false);
    const auto setFile = studio.getDocument().getFolder().getChildFile ("samples").getChildFile (fileName);
    auto destFolder = studio.getUserSamplesFolder();
    destFolder.createDirectory();
    const auto dest = destFolder.getChildFile (fileName);

    bool ok = false;
    if (setFile.existsAsFile())
        ok = setFile.copyFileTo (dest);
    else if (auto sample = currentSample())
        ok = SampleLibrary::writeWav (*sample, dest);

    flash (ok ? "Saved to Recordings" : "The sample could not be saved");
}

void SampleView::showOptions()
{
    auto* t = track();
    if (t == nullptr)
        return;

    const bool drum = t->isDrum();
    const auto ref = currentRef();

    juce::PopupMenu menu;
    menu.addSectionHeader (drum ? "Pad " + juce::String (t->perf.selectedPad + 1) : juce::String ("Sampler"));

    const auto chokeTarget = paramTarget ("choke");
    if (drum && chokeTarget.paramIndex >= 0)
        menu.addItem (1, "Choke", true, paramValue ("choke", 0.0f) >= 0.5f);

    const auto modeTarget = paramTarget ("envMode");
    if (modeTarget.paramIndex >= 0)
    {
        auto* spec = ParamKnob::specFor (studio.getSet(), trackId, modeTarget);
        const int current = juce::roundToInt (paramValue ("envMode", 0.0f));
        juce::PopupMenu modes;
        modes.addItem (10, "Trigger", true, current == 0);
        modes.addItem (11, "Gate", true, current == 1);
        menu.addSubMenu ("Mode: " + juce::String (current == 1 ? "Gate" : "Trigger"), modes, spec != nullptr);
    }

    menu.addSeparator();
    menu.addItem (2, "Save Sample", isSetRecording (ref));
    menu.addItem (3, "Swap Sample...");

    juce::Component::SafePointer<SampleView> safe (this);
    showMenu (menu, optionsButton, [safe] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        switch (result)
        {
            case 1:  safe->setParam ("choke", safe->paramValue ("choke", 0.0f) >= 0.5f ? 0.0f : 1.0f, false); break;
            case 10: safe->setParam ("envMode", 0.0f, false); break;
            case 11: safe->setParam ("envMode", 1.0f, false); break;
            case 2:  safe->saveSampleToLibrary(); break;
            case 3:  safe->loadSample(); break;
            default: break;
        }
    });
}

//==============================================================================
// Button panel width for the current height: a column of three when tall, a 2x2 grid in the
// usual portrait size, a row of three when the panel is squeezed (landscape).
static int sampleViewButtonWidth (int height)
{
    return height >= 150 ? 64 : (height >= 90 ? 96 : 3 * Theme::touch);
}

juce::Rectangle<int> SampleView::waveArea() const
{
    return getLocalBounds().withTrimmedRight (sampleViewButtonWidth (getHeight()) + 6);
}

void SampleView::resized()
{
    auto bounds = getLocalBounds();
    if (getHeight() < 90)
    {
        auto row = bounds.removeFromRight (sampleViewButtonWidth (getHeight()));
        loadButton.setBounds (row.removeFromLeft (Theme::touch));
        recordButton.setBounds (row.removeFromLeft (Theme::touch));
        optionsButton.setBounds (row);
    }
    else if (getHeight() >= 150)
    {
        auto column = bounds.removeFromRight (64);
        const int h = column.getHeight() / 3;
        loadButton.setBounds (column.removeFromTop (h));
        optionsButton.setBounds (column.removeFromBottom (h));
        recordButton.setBounds (column);
    }
    else
    {
        auto panel = bounds.removeFromRight (96);
        auto top = panel.removeFromTop (panel.getHeight() / 2);
        loadButton.setBounds (top.removeFromLeft (48));
        optionsButton.setBounds (top);
        recordButton.setBounds (panel);
    }
}

void SampleView::clampView()
{
    zoom = juce::jlimit (1.0, sampleViewMaxZoom, zoom);
    viewStart = juce::jlimit (0.0, 1.0 - 1.0 / zoom, viewStart);
}

void SampleView::setZoom (double newZoom, double anchorFraction, float anchorX)
{
    auto wave = waveArea().toFloat();
    zoom = juce::jlimit (1.0, sampleViewMaxZoom, newZoom);
    const double t = wave.getWidth() > 0.0f ? (anchorX - wave.getX()) / wave.getWidth() : 0.5;
    viewStart = anchorFraction - t / zoom;
    clampView();
    repaint();
}

void SampleView::paint (juce::Graphics& g)
{
    const auto& t = Theme::get();
    auto* tr = track();
    if (tr == nullptr)
        return;

    const auto colour = Theme::trackColour (tr->colour);
    auto area = waveArea().toFloat();

    g.setColour (t.surface);
    g.fillRoundedRectangle (area, Theme::corner);
    if (recording)
    {
        g.setColour (t.record.withAlpha (0.14f));
        g.fillRoundedRectangle (area, Theme::corner);
        g.setColour (t.record);
        g.drawRoundedRectangle (area.reduced (0.5f), Theme::corner, 1.5f);
    }

    auto inner = area.reduced (8.0f, 6.0f);
    const bool compact = inner.getHeight() < 70.0f; // info drawn over the waveform
    auto infoRow = compact ? inner.withHeight (16.0f) : inner.removeFromTop (16.0f);

    // Recording state: elapsed time, level meter, where it goes (SMP-3).
    if (recording || waitingForPermission)
    {
        g.setColour (t.record);
        g.fillEllipse (infoRow.removeFromLeft (10.0f).withSizeKeepingCentre (9.0f, 9.0f));
        infoRow.removeFromLeft (6.0f);
        g.setFont (Theme::font (13.0f, true));
        if (waitingForPermission)
        {
            g.drawText ("Waiting for the microphone...", infoRow, juce::Justification::centredLeft);
            return;
        }

        const double elapsed = (sampleViewNowMs() - recordStartMs) / 1000.0;
        g.drawText ("REC " + sampleViewFormatSeconds (elapsed), infoRow, juce::Justification::centredLeft);
        g.setColour (t.textDim);
        g.setFont (Theme::font (12.0f));
        const auto target = recordPad >= 0 ? "Pad " + juce::String (recordPad + 1) : juce::String ("Sampler");
        g.drawText (target + "  -  " + (source == Source::microphone ? "Microphone" : "Resample"), infoRow,
                    juce::Justification::centredRight);

        // Level meter.
        auto meterArea = compact ? area.reduced (10.0f, 0.0f).withTop (area.getBottom() - 5.0f).withHeight (3.0f)
                                 : inner.withSizeKeepingCentre (inner.getWidth(), juce::jmin (14.0f, inner.getHeight() * 0.25f));
        const float level = studio.getSession().getRecordLevel();
        const float db = juce::Decibels::gainToDecibels (level, -60.0f);
        const float fill = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        g.setColour (t.surfaceRaised);
        g.fillRoundedRectangle (meterArea, 4.0f);
        g.setColour (db > -3.0f ? t.record : (db > -12.0f ? t.accent : t.playing));
        g.fillRoundedRectangle (meterArea.withWidth (meterArea.getWidth() * fill), 4.0f);

        if (! compact)
        {
            // Progress towards the maximum length.
            auto progress = inner.removeFromBottom (3.0f);
            g.setColour (t.record.withAlpha (0.6f));
            g.fillRect (progress.withWidth (progress.getWidth() * (float) juce::jlimit (0.0, 1.0, elapsed / sampleViewMaxRecordSeconds)));

            g.setColour (t.textDim);
            g.setFont (Theme::font (11.5f));
            g.drawText (recordPad >= 0 ? "Tap another pad to record into it" : "Tap stop to finish",
                        inner.removeFromBottom (14.0f), juce::Justification::centred);
        }
        return;
    }

    auto sample = currentSample();
    if (sample == nullptr)
    {
        g.setColour (t.textDim);
        g.setFont (Theme::font (14.0f, true));
        const auto label = currentRef().isEmpty() ? juce::String ("No sample") : juce::String ("Sample not found");
        g.drawText (label, inner.withTrimmedBottom (inner.getHeight() * 0.25f), juce::Justification::centred);
        if (! compact)
        {
            g.setFont (Theme::font (12.0f));
            g.drawText ("Load one or record with the mic", inner.withTrimmedTop (inner.getHeight() * 0.5f),
                        juce::Justification::centredTop);
        }
    }
    else
    {
        if (peaks.getSample() != sample.get())
            peaks = WaveformPeaks (sample);

        const float gainDb = paramValue ("gain", 0.0f);
        const float startFraction = paramValue ("sampleStart", 0.0f);
        const float scale = juce::Decibels::decibelsToGain (gainDb, -100.0f);

        // Waveform.
        auto wave = inner.withTrimmedTop (2.0f);
        const int columns = juce::jmax (1, (int) wave.getWidth());
        const double frames = (double) peaks.getNumFrames();
        const double from = viewStart * frames;
        const double to = (viewStart + 1.0 / zoom) * frames;
        auto cols = peaks.getColumns (from, to, columns);

        const float mid = wave.getCentreY();
        const float half = wave.getHeight() * 0.5f;
        juce::RectangleList<float> bars;
        for (int x = 0; x < columns; ++x)
        {
            const auto& c = cols[(size_t) x];
            const float top = mid - juce::jlimit (0.0f, 1.0f, c.max * scale) * half;
            const float bottom = mid - juce::jlimit (-1.0f, 0.0f, c.min * scale) * half;
            bars.addWithoutMerging ({ wave.getX() + (float) x, top, 1.0f, juce::jmax (1.0f, bottom - top) });
        }
        g.setColour (colour);
        g.fillRectList (bars);

        g.setColour (t.outline.withAlpha (0.6f));
        g.fillRect (wave.getX(), mid, wave.getWidth(), 1.0f);

        // Start point: everything before it is dimmed.
        const double startPos = (startFraction - viewStart) * zoom;
        const float sx = wave.getX() + (float) startPos * wave.getWidth();
        if (sx > wave.getX())
        {
            g.setColour (t.surface.withAlpha (0.72f));
            g.fillRect (juce::Rectangle<float> (wave.getX(), wave.getY(), juce::jmin (sx, wave.getRight()) - wave.getX(), wave.getHeight()));
        }
        if (sx >= wave.getX() - 1.0f && sx <= wave.getRight() + 1.0f)
        {
            g.setColour (t.text);
            g.fillRect (sx - 1.0f, wave.getY(), 2.0f, wave.getHeight());
            juce::Path handle;
            handle.addTriangle (sx - 6.0f, wave.getY(), sx + 6.0f, wave.getY(), sx, wave.getY() + 8.0f);
            g.fillPath (handle);
        }

        // Name, length, gain, zoom (left out when the view is squeezed very flat).
        const bool tiny = inner.getHeight() < 36.0f;
        juce::String name = sampleDisplayName (currentRef());
        if (tr->isDrum() && tr->pads[(size_t) tr->perf.selectedPad].name.isNotEmpty())
            name = tr->pads[(size_t) tr->perf.selectedPad].name;
        if (name.isEmpty())
            name = sample->name;

        if (! tiny)
        {
            g.setFont (Theme::font (12.5f, true));
            g.setColour (t.text);
            g.drawFittedText (name, infoRow.withTrimmedRight (80.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        }

        auto* gainSpec = ParamKnob::specFor (studio.getSet(), trackId, paramTarget ("gain"));
        if (gainSpec != nullptr && ! tiny)
        {
            g.setColour (drag == Drag::gain ? t.text : t.textDim);
            g.setFont (Theme::font (12.5f, drag == Drag::gain));
            g.drawText (gainSpec->format (gainDb), infoRow, juce::Justification::centredRight);
        }

        g.setFont (Theme::font (11.0f));
        g.setColour (t.textDim);
        juce::String footer = juce::String (sample->getLengthSeconds(), 2) + " s";
        if (zoom > 1.01)
            footer << "   " << juce::String (zoom, 1) << "x";
        if (! compact)
            g.drawText (footer, inner.removeFromBottom (13.0f), juce::Justification::bottomRight);
    }

    if (flashMessage.isNotEmpty())
    {
        auto bubble = area.withSizeKeepingCentre (juce::jmin (area.getWidth() - 16.0f, 260.0f), 30.0f);
        g.setColour (t.surfaceRaised.withAlpha (0.95f));
        g.fillRoundedRectangle (bubble, 15.0f);
        g.setColour (t.text);
        g.setFont (Theme::font (13.0f, true));
        g.drawFittedText (flashMessage, bubble.reduced (10.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }
}

//==============================================================================
void SampleView::mouseDown (const juce::MouseEvent& e)
{
    if (recording || ! waveArea().contains (e.getPosition()))
        return;

    touches[e.source.getIndex()] = e.position;

    if (touches.size() >= 2)
    {
        // Second finger: pinch zoom replaces whatever the first finger was doing.
        if (gestureOpen)
        {
            studio.getDocument().endGesture();
            gestureOpen = false;
        }
        auto it = touches.begin();
        const auto a = it->second;
        const auto b = (++it)->second;
        pinchStartDistance = juce::jmax (8.0f, a.getDistanceFrom (b));
        pinchStartZoom = zoom;
        auto wave = waveArea().toFloat();
        const float midX = (a.x + b.x) * 0.5f;
        pinchAnchor = viewStart + (midX - wave.getX()) / juce::jmax (1.0f, wave.getWidth()) / zoom;
        drag = Drag::pinch;
        return;
    }

    drag = Drag::undecided;
    downPosition = e.position;
    dragStartFraction = paramValue ("sampleStart", 0.0f);
    if (auto* spec = ParamKnob::specFor (studio.getSet(), trackId, paramTarget ("gain")))
        dragStartGainNormalised = spec->toNormalised (paramValue ("gain", 0.0f));
}

void SampleView::mouseDrag (const juce::MouseEvent& e)
{
    if (touches.count (e.source.getIndex()) == 0)
        return;
    touches[e.source.getIndex()] = e.position;

    if (drag == Drag::pinch)
    {
        if (touches.size() < 2)
            return;
        auto it = touches.begin();
        const auto a = it->second;
        const auto b = (++it)->second;
        const float distance = juce::jmax (8.0f, a.getDistanceFrom (b));
        setZoom (pinchStartZoom * distance / pinchStartDistance, pinchAnchor, (a.x + b.x) * 0.5f);
        return;
    }

    const auto delta = e.position - downPosition;
    if (drag == Drag::undecided)
    {
        if (std::abs (delta.x) < 6.0f && std::abs (delta.y) < 6.0f)
            return;
        const bool horizontal = std::abs (delta.x) >= std::abs (delta.y);
        const auto target = paramTarget (horizontal ? "sampleStart" : "gain");
        if (target.paramIndex < 0 || currentSample() == nullptr)
        {
            drag = Drag::none;
            return;
        }
        drag = horizontal ? Drag::start : Drag::gain;
        studio.getDocument().beginGesture(); // one undo step per drag
        gestureOpen = true;
    }

    auto wave = waveArea().toFloat();
    if (drag == Drag::start)
    {
        const float fraction = juce::jlimit (0.0f, 1.0f, dragStartFraction + delta.x / juce::jmax (1.0f, wave.getWidth()) / (float) zoom);
        if (std::abs (fraction - paramValue ("sampleStart", 0.0f)) > 1.0e-6f)
            setParam ("sampleStart", fraction, true);

        // Keep the start point in view while zoomed in.
        if (fraction < viewStart || fraction > viewStart + 1.0 / zoom)
        {
            viewStart = fraction - 0.5 / zoom;
            clampView();
        }
        repaint();
    }
    else if (drag == Drag::gain)
    {
        if (auto* spec = ParamKnob::specFor (studio.getSet(), trackId, paramTarget ("gain")))
        {
            const float value = spec->fromNormalised (juce::jlimit (0.0f, 1.0f, dragStartGainNormalised - delta.y / 200.0f));
            if (std::abs (value - paramValue ("gain", 0.0f)) > 1.0e-4f)
                setParam ("gain", value, true);
        }
        repaint();
    }
}

void SampleView::mouseUp (const juce::MouseEvent& e)
{
    touches.erase (e.source.getIndex());

    if (drag == Drag::pinch)
    {
        if (touches.empty())
            drag = Drag::none;
        return;
    }

    if (gestureOpen)
    {
        studio.getDocument().endGesture();
        gestureOpen = false;
    }
    drag = Drag::none;
    repaint();
}

void SampleView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (waveArea().contains (e.getPosition()))
    {
        zoom = 1.0;
        viewStart = 0.0;
        repaint();
    }
}

void SampleView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    auto wave = waveArea().toFloat();
    const double anchor = viewStart + (e.position.x - wave.getX()) / juce::jmax (1.0f, wave.getWidth()) / zoom;
    setZoom (zoom * scaleFactor, anchor, e.position.x);
}

void SampleView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        mouseMagnify (e, 1.0f + wheel.deltaY);
        return;
    }
    if (zoom > 1.0)
    {
        viewStart -= (std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY) / zoom;
        clampView();
        repaint();
    }
}

} // namespace rb
