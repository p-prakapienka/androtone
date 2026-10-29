namespace rb
{

Studio::Studio (const juce::File& dataRoot)
    : library (dataRoot),
      settings (dataRoot.getChildFile ("settings.xml"))
{
    engine.getSamples().setUserFolder (library.getUserSamplesFolder());
    engine.start (false);
    engine.getSession().setMonitorInput (settings.getMonitorInput());
    library.ensureDemos();

    midiInputs = std::make_unique<MidiInputs> (*this);

    getDocument().addListener (this);
    startTimerHz (30);
}

Studio::~Studio()
{
    stopTimer();
    midiInputs = nullptr;
    getDocument().removeListener (this);
    getDocument().close();
}

void Studio::post (Command::Type type, int trackId, int slot, int pitch, float velocity)
{
    Command c;
    c.type = type;
    c.trackId = trackId;
    c.slot = slot;
    c.pitch = pitch;
    c.velocity = velocity;
    engine.getSession().post (c);
}

//==============================================================================
void Studio::openSet (const SetInfo& info)
{
    stopPreview();
    stop();
    capture.clearAll();
    pendingAutomation.clear();

    getDocument().open (info.folder, Set {});
    openSetId = info.id;
    settings.setLastOpenedSet (info.id);
    listeners.call ([] (Listener& l) { l.studioChanged(); });
}

void Studio::closeSet()
{
    stop();
    post (Command::Type::stopAllClips);
    getDocument().close();
    getDocument().openInMemory (Set {});
    openSetId = {};
    capture.clearAll();
    pendingAutomation.clear();
    listeners.call ([] (Listener& l) { l.studioChanged(); });
}

void Studio::previewSet (const SetInfo& info)
{
    if (isSetOpen())
        return;

    stop();
    getDocument().open (info.folder, Set {});
    previewSetId = info.id;
    post (Command::Type::stopAllClips);
    play();
    listeners.call ([] (Listener& l) { l.studioChanged(); });
}

void Studio::stopPreview()
{
    if (previewSetId.isEmpty())
        return;

    stop();
    getDocument().close();
    getDocument().openInMemory (Set {});
    previewSetId = {};
    listeners.call ([] (Listener& l) { l.studioChanged(); });
}

//==============================================================================
void Studio::play()   { post (Command::Type::play); }
void Studio::stop()   { post (Command::Type::stop); }

void Studio::togglePlay()
{
    if (isPlaying()) stop();
    else play();
}

void Studio::launchClip (int trackId, int slot)        { post (Command::Type::launchClip, trackId, slot); }
void Studio::stopClip (int trackId)                     { post (Command::Type::stopClip, trackId); }
void Studio::launchScene (int scene)                    { post (Command::Type::launchScene, 0, scene); }
void Studio::switchClipSeamless (int trackId, int slot) { post (Command::Type::switchClipSeamless, trackId, slot); }

int Studio::getPlayingSlot (int trackId) const
{
    auto* state = engine.getSession().findTrackState (trackId);
    return state != nullptr ? state->playingSlot.load() : -1;
}

int Studio::getQueuedSlot (int trackId) const
{
    auto* state = engine.getSession().findTrackState (trackId);
    return state != nullptr ? state->queuedSlot.load() : SessionProcessor::noQueue;
}

double Studio::getClipPosition (int trackId) const
{
    auto* state = engine.getSession().findTrackState (trackId);
    return state != nullptr ? state->clipBeat.load() : 0.0;
}

//==============================================================================
void Studio::noteOn (int trackId, int pitch, float velocity)
{
    post (Command::Type::noteOn, trackId, 0, pitch, juce::jlimit (0.01f, 1.0f, velocity));
}

void Studio::noteOff (int trackId, int pitch)
{
    post (Command::Type::noteOff, trackId, 0, pitch);
}

void Studio::allNotesOff (int trackId)
{
    post (Command::Type::allLiveNotesOff, trackId);
}

bool Studio::isNoteSounding (int trackId, int pitch) const
{
    auto* state = engine.getSession().findTrackState (trackId);
    return state != nullptr && state->isNoteSounding (pitch);
}

//==============================================================================
bool Studio::canCaptureFree (int trackId) const
{
    return capture.hasFreeNotes (trackId);
}

bool Studio::hasPendingNotes (int trackId, int slot) const
{
    for (auto& n : capture.getNotes (trackId))
        if (n.slot == slot && slot >= 0)
            return true;
    return false;
}

std::vector<Note> Studio::getPendingNotes (int trackId, int slot)
{
    auto* track = getSet().findTrack (trackId);
    if (track == nullptr || slot < 0 || track->slots[(size_t) slot] == nullptr)
        return {};
    return capture.pendingForClip (trackId, slot, *track->slots[(size_t) slot]);
}

int Studio::captureFree (int trackId, int preferredSlot)
{
    const auto& set = getSet();
    auto* track = set.findTrack (trackId);
    if (track == nullptr || ! capture.hasFreeNotes (trackId))
        return -1;

    const bool running = isPlaying();
    const bool detectTempo = ! running && ! set.hasAnyClip();
    const int minBars = running ? set.shortestClipBars() : 0;

    auto result = capture.captureFree (trackId, set.tempo, detectTempo, minBars);
    if (result.clip == nullptr)
        return -1;

    int slot = -1;
    if (preferredSlot >= 0 && preferredSlot < numScenes && track->slots[(size_t) preferredSlot] == nullptr)
        slot = preferredSlot;
    for (int s = 0; s < numScenes && slot < 0; ++s)
        if (track->slots[(size_t) s] == nullptr)
            slot = s;
    if (slot < 0)
        return -1;

    getDocument().edit ([&] (Set& s)
    {
        setClip (s, trackId, slot, result.clip);
        if (result.tempo > 0.0)
            s.tempo = result.tempo;
    });

    capture.clear (trackId);
    launchClip (trackId, slot);
    listeners.call ([] (Listener& l) { l.captureChanged(); });
    return slot;
}

void Studio::addPending (int trackId, int slot)
{
    auto* track = getSet().findTrack (trackId);
    if (track == nullptr || slot < 0 || slot >= numScenes || track->slots[(size_t) slot] == nullptr)
        return;

    auto notes = capture.pendingForClip (trackId, slot, *track->slots[(size_t) slot]);
    auto lanes = pendingAutomation[trackId];

    if (notes.empty() && lanes.empty())
        return;

    getDocument().edit ([&] (Set& s)
    {
        auto* t = s.findTrack (trackId);
        auto clip = t->slots[(size_t) slot];
        auto updated = notes.empty() ? std::make_shared<Clip> (*clip) : std::make_shared<Clip> (*mergeNotes (*clip, notes));

        for (auto& pending : lanes)
        {
            if (pending.points.empty())
                continue;

            auto points = pending.points;
            std::sort (points.begin(), points.end(), [] (auto& a, auto& b) { return a.beat < b.beat; });
            const double from = points.front().beat, to = points.back().beat;

            AutomationLane lane;
            lane.target = pending.target;
            if (auto* existing = updated->findLane (pending.target))
                for (auto& p : existing->points)
                    if (p.beat < from || p.beat > to)
                        lane.points.push_back (p);
            lane.points.insert (lane.points.end(), points.begin(), points.end());
            std::sort (lane.points.begin(), lane.points.end(), [] (auto& a, auto& b) { return a.beat < b.beat; });

            auto& automation = updated->automation;
            automation.erase (std::remove_if (automation.begin(), automation.end(),
                                              [&] (const AutomationLane& l) { return l.target == lane.target; }),
                              automation.end());
            automation.push_back (std::move (lane));
        }

        t->slots[(size_t) slot] = updated;
    });

    capture.clearPendingForClip (trackId, slot);
    pendingAutomation.erase (trackId);
    listeners.call ([] (Listener& l) { l.captureChanged(); });
}

void Studio::clearCapture (int trackId)
{
    capture.clear (trackId);
    listeners.call ([] (Listener& l) { l.captureChanged(); });
}

void Studio::createFixedLengthClip (int trackId, int slot, int bars)
{
    auto* track = getSet().findTrack (trackId);
    if (track == nullptr || slot < 0 || slot >= numScenes)
        return;

    getDocument().edit ([&] (Set& s) { setClip (s, trackId, slot, makeEmptyClip (bars)); });
    launchClip (trackId, slot);
}

//==============================================================================
void Studio::recordParameterChange (int trackId, const ParamTarget& target, float value)
{
    if (! isPlaying())
        return;

    const int slot = getPlayingSlot (trackId);
    if (slot < 0)
        return;

    auto& lanes = pendingAutomation[trackId];
    auto it = std::find_if (lanes.begin(), lanes.end(), [&] (auto& l) { return l.target == target; });
    if (it == lanes.end())
    {
        lanes.push_back ({ target, {} });
        it = lanes.end() - 1;
    }

    const double position = getClipPosition (trackId);

    // Drop older points this pass overwrites (the recording wraps with the loop).
    auto& points = it->points;
    if (! points.empty() && position < points.back().beat)
        points.erase (std::remove_if (points.begin(), points.end(), [&] (auto& p) { return p.beat >= position; }),
                      points.end());

    points.push_back ({ position, value });
    listeners.call ([] (Listener& l) { l.captureChanged(); });
}

const std::vector<Studio::PendingAutomation>& Studio::getPendingAutomation (int trackId) const
{
    static const std::vector<PendingAutomation> empty;
    if (auto it = pendingAutomation.find (trackId); it != pendingAutomation.end())
        return it->second;
    return empty;
}

void Studio::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    if (! tapTimes.empty() && now - tapTimes.back() > 2.0)
        tapTimes.clear();

    tapTimes.push_back (now);
    if (tapTimes.size() > 8)
        tapTimes.erase (tapTimes.begin());

    // Tempo follows the taps from the fourth one on (SET-1).
    if (tapTimes.size() >= 4)
    {
        const double interval = (tapTimes.back() - tapTimes.front()) / (double) (tapTimes.size() - 1);
        if (interval > 0.0)
        {
            const double bpm = juce::jlimit (minTempo, maxTempo, std::round (600.0 / interval) / 10.0);
            getDocument().edit ([bpm] (Set& s) { s.tempo = bpm; });
        }
    }
}

//==============================================================================
void Studio::timerCallback()
{
    auto& session = engine.getSession();
    bool any = false;

    for (;;)
    {
        const int n = session.readCaptureEvents (eventBuffer.data(), (int) eventBuffer.size());
        for (int i = 0; i < n; ++i)
            capture.add (eventBuffer[(size_t) i]);
        any = any || n > 0;
        if (n < (int) eventBuffer.size())
            break;
    }

    const bool hadFree = [&]
    {
        for (auto& t : getSet().tracks)
            if (capture.hasFreeNotes (t.id))
                return true;
        return false;
    }();

    capture.update (session.getElapsedSeconds(), settings.getCaptureTimeout());

    bool hasFree = false;
    for (auto& t : getSet().tracks)
        hasFree = hasFree || capture.hasFreeNotes (t.id);

    // Pending automation belongs to the clip that was playing; drop it if playback moved on.
    for (auto it = pendingAutomation.begin(); it != pendingAutomation.end();)
        it = (! isPlaying() || getPlayingSlot (it->first) < 0) ? pendingAutomation.erase (it) : std::next (it);

    session.collectGarbage();

    if (any || hadFree != hasFree)
        listeners.call ([] (Listener& l) { l.captureChanged(); });
}

void Studio::setActiveTrackId (int trackId)
{
    preferredActiveTrackId = trackId;
    documentChanged();
}

void Studio::documentChanged()
{
    auto& set = getSet();
    const int active = set.findTrack (preferredActiveTrackId) != nullptr ? preferredActiveTrackId
                     : (set.tracks.empty() ? 0 : set.tracks.front().id);
    activeTrackId.store (active);

    listeners.call ([] (Listener& l) { l.studioChanged(); });
}

} // namespace rb
