namespace rb
{

MidiInputs::MidiInputs (Studio& s) : studio (s)
{
    listConnection = juce::MidiDeviceListConnection::make ([this]
    {
        refresh();
        if (onChange != nullptr)
            onChange();
    });
    refresh();
}

MidiInputs::~MidiInputs()
{
    cancelPendingUpdate();
    for (auto& [id, input] : openInputs)
        input->stop();
    openInputs.clear();
}

std::vector<MidiInputs::Device> MidiInputs::getDevices() const
{
    const auto enabled = studio.getSettings().getEnabledMidiInputs();
    std::vector<Device> devices;
    for (auto& info : juce::MidiInput::getAvailableDevices())
        devices.push_back ({ info, enabled.contains (info.identifier), openInputs.count (info.identifier) > 0 });
    return devices;
}

void MidiInputs::setEnabled (const juce::String& identifier, bool enabled)
{
    auto ids = studio.getSettings().getEnabledMidiInputs();
    ids.removeString (identifier);
    if (enabled)
        ids.add (identifier);
    studio.getSettings().setEnabledMidiInputs (ids);
    refresh();
    if (onChange != nullptr)
        onChange();
}

void MidiInputs::refresh()
{
    // Remembered devices reconnect automatically when they appear again (MIDI-1).
    const auto enabled = studio.getSettings().getEnabledMidiInputs();
    const auto available = juce::MidiInput::getAvailableDevices();

    for (auto it = openInputs.begin(); it != openInputs.end();)
    {
        const bool stillWanted = enabled.contains (it->first)
                              && std::any_of (available.begin(), available.end(), [&] (auto& d) { return d.identifier == it->first; });
        if (! stillWanted)
        {
            it->second->stop();
            it = openInputs.erase (it);
        }
        else
        {
            ++it;
        }
    }

    for (auto& info : available)
        if (enabled.contains (info.identifier) && openInputs.count (info.identifier) == 0)
            if (auto input = juce::MidiInput::openDevice (info.identifier, this))
            {
                input->start();
                openInputs[info.identifier] = std::move (input);
            }
}

const juce::StringArray& MidiInputs::getTargets()
{
    static const juce::StringArray targets { "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Macro 5", "Macro 6",
                                             "Macro 7", "Macro 8", "Capture", "Clear Capture", "Start/Stop",
                                             "Quantize", "Undo", "Redo" };
    return targets;
}

void MidiInputs::startLearning (const juce::String& target)
{
    {
        const std::lock_guard<std::mutex> lock (learnMutex);
        learningTarget = target;
    }
    if (onChange != nullptr)
        onChange();
}

void MidiInputs::cancelLearning()
{
    startLearning ({});
}

juce::String MidiInputs::getLearningTarget() const
{
    auto& self = const_cast<MidiInputs&> (*this);
    const std::lock_guard<std::mutex> lock (self.learnMutex);
    return learningTarget;
}

void MidiInputs::clearMapping (const juce::String& target)
{
    studio.getSettings().setMidiMapping (target, {});
    if (onChange != nullptr)
        onChange();
}

juce::String MidiInputs::sourceKey (const juce::MidiMessage& m)
{
    if (m.isController())
        return "cc:" + juce::String (m.getChannel()) + ":" + juce::String (m.getControllerNumber());
    if (m.isNoteOn())
        return "note:" + juce::String (m.getChannel()) + ":" + juce::String (m.getNoteNumber());
    return {};
}

void MidiInputs::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    handleMessage (m);
}

void MidiInputs::handleMessage (const juce::MidiMessage& m)
{
    // Notes go straight to the engine for low latency (MIDI-2) unless a note is being learned
    // or mapped to a command.
    bool learning;
    {
        const std::lock_guard<std::mutex> lock (learnMutex);
        learning = learningTarget.isNotEmpty();
    }

    const int trackId = studio.getActiveTrackId();

    if (! learning && (m.isNoteOn() || m.isNoteOff()) && trackId != 0)
    {
        if (m.isNoteOn())
            studio.noteOn (trackId, m.getNoteNumber(), m.getFloatVelocity());
        else
            studio.noteOff (trackId, m.getNoteNumber());
    }

    // Pitch bend and pressure (MIDI-2): poly aftertouch per note, channel pressure for all.
    if (trackId != 0)
    {
        if (m.isPitchWheel())
            studio.pitchBend (trackId, (float) (m.getPitchWheelValue() - 8192) / 8192.0f);
        else if (m.isAftertouch())
            studio.pressure (trackId, m.getNoteNumber(), (float) m.getAfterTouchValue() / 127.0f);
        else if (m.isChannelPressure())
            studio.pressure (trackId, -1, (float) m.getChannelPressureValue() / 127.0f);
    }

    if (m.isController() || m.isNoteOn())
    {
        const juce::ScopedLock sl (pendingLock);
        pending.push_back (m);
        triggerAsyncUpdate();
    }
}

void MidiInputs::handleAsyncUpdate()
{
    std::vector<juce::MidiMessage> messages;
    {
        const juce::ScopedLock sl (pendingLock);
        std::swap (messages, pending);
    }

    auto& settings = studio.getSettings();

    for (auto& m : messages)
    {
        const auto key = sourceKey (m);
        if (key.isEmpty())
            continue;

        const auto target = getLearningTarget();
        if (target.isNotEmpty())
        {
            // The first control moved while learning becomes the mapping.
            for (auto& t : getTargets())
                if (settings.getMidiMapping (t) == key)
                    settings.setMidiMapping (t, {});
            settings.setMidiMapping (target, key);
            cancelLearning();
            continue;
        }

        for (auto& t : getTargets())
            if (settings.getMidiMapping (t) == key)
                runMappedAction (t, m.isController() ? m.getControllerValue() : 127);
    }
}

void MidiInputs::runMappedAction (const juce::String& target, int value)
{
    const int trackId = studio.getActiveTrackId();
    const bool pressed = value >= 64;

    if (target.startsWith ("Macro"))
    {
        auto* track = studio.getSet().findTrack (trackId);
        if (track == nullptr)
            return;

        // Macros drive the preset's macro parameters, or the first eight instrument
        // parameters when a preset defines none.
        const int macro = target.getTrailingIntValue() - 1;
        const auto& spec = getInstrumentSpec (track->instrumentType);
        int index = -1;
        if (macro < track->macros.size())
            index = spec.indexOf (track->macros[macro]);
        else if (track->macros.isEmpty() && macro < (int) spec.params.size())
            index = macro;
        if (index < 0)
            return;

        const ParamTarget param { ParamTarget::instrument, 0, index };
        const float v = spec.params[(size_t) index].fromNormalised (value / 127.0f);
        studio.getDocument().beginGesture();
        studio.getDocument().edit ([=] (Set& s) { ParamKnob::setValue (s, trackId, param, v); });
        studio.getDocument().endGesture();
        studio.recordParameterChange (trackId, param, v);
        return;
    }

    if (! pressed)
        return;

    if (target == "Capture")
    {
        const int playing = studio.getPlayingSlot (trackId);
        if (playing >= 0 && studio.hasPendingNotes (trackId, playing))
            studio.addPending (trackId, playing);
        else
            studio.captureFree (trackId);
    }
    else if (target == "Clear Capture") studio.clearCapture (trackId);
    else if (target == "Start/Stop")    studio.togglePlay();
    else if (target == "Undo")          studio.getDocument().undo();
    else if (target == "Redo")          studio.getDocument().redo();
    else if (target == "Quantize")
    {
        const int slot = studio.getPlayingSlot (trackId);
        const double grid = InstrumentView::quantizeTriplets ? tripletOf (InstrumentView::quantizeGrid) : InstrumentView::quantizeGrid;
        const float strength = InstrumentView::quantizeStrength;
        studio.getDocument().edit ([=] (Set& s)
        {
            if (auto* t = s.findTrack (trackId))
                if (slot >= 0)
                    if (auto& clip = t->slots[(size_t) slot])
                        clip = quantizeNotes (*clip, selectAll (*clip), grid, strength).clip;
        });
    }
}

} // namespace rb
