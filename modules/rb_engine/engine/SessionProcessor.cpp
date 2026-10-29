namespace rb
{

static constexpr double beatEpsilon = 1.0e-7;

// First sample at or after `clock` inside a block starting at clock0.
static int sampleOffsetOf (double clock, double clock0, double beatsPerSample, int numSamples)
{
    return juce::jlimit (0, numSamples - 1, (int) std::ceil ((clock - clock0) / beatsPerSample - 1.0e-6));
}

SessionProcessor::SessionProcessor() = default;
SessionProcessor::~SessionProcessor() = default;

//==============================================================================
void SessionProcessor::prepare (double newSampleRate, int newMaxBlockSize)
{
    std::lock_guard<std::recursive_mutex> lock (prepareMutex);

    sampleRate = newSampleRate;
    maxBlockSize = juce::jmax (16, newMaxBlockSize);

    dryBuffer.setSize (2, maxBlockSize);
    sendBuffer.setSize (2, maxBlockSize);

    for (auto& st : states)
    {
        st.gain.reset (sampleRate, 0.02);
        st.numOffs = 0;
        st.soundingCount.fill (0);
    }

    if (published != nullptr)
    {
        for (auto& track : published->tracks)
        {
            if (track.instrument != nullptr)
                track.instrument->prepare (sampleRate, maxBlockSize);
            for (auto& fx : track.effects)
                if (fx != nullptr)
                    fx->prepare (sampleRate, maxBlockSize);
        }
    }

    clickDecay = std::exp (-1.0f / (float) (sampleRate * 0.025));
    active = nullptr; // forces the published snapshot to be applied again
    audioActive.store (true);
}

void SessionProcessor::releaseResources()
{
    std::lock_guard<std::recursive_mutex> lock (prepareMutex);
    audioActive.store (false);
    active = nullptr;
}

//==============================================================================
void SessionProcessor::publish (SnapshotPtr snapshot)
{
    std::lock_guard<std::recursive_mutex> lock (prepareMutex);

    if (published != nullptr)
        retired.push_back (published);

    published = std::move (snapshot);
    pending.store (published.get(), std::memory_order_release);
    collectGarbage();
}

void SessionProcessor::collectGarbage()
{
    std::lock_guard<std::recursive_mutex> lock (prepareMutex);

    const auto acked = acknowledged.load();
    const bool idle = ! audioActive.load();

    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [&] (const SnapshotPtr& s) { return idle || s->sequence < acked; }),
                   retired.end());
}

bool SessionProcessor::post (const Command& command)
{
    // Commands may come from the message thread and the MIDI input thread; producers
    // serialise here, the audio thread (the only consumer) never takes this lock.
    const std::lock_guard<std::mutex> lock (postMutex);
    const auto scope = commandFifo.write (1);
    if (scope.blockSize1 + scope.blockSize2 == 0)
        return false;

    if (scope.blockSize1 > 0)
        commands[(size_t) scope.startIndex1] = command;
    else
        commands[(size_t) scope.startIndex2] = command;
    return true;
}

int SessionProcessor::readCaptureEvents (CaptureEvent* destination, int maxEvents)
{
    const auto scope = captureFifo.read (juce::jmin (maxEvents, captureFifo.getNumReady()));
    int n = 0;
    for (int i = 0; i < scope.blockSize1; ++i) destination[n++] = captures[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i) destination[n++] = captures[(size_t) (scope.startIndex2 + i)];
    return n;
}

const SessionProcessor::TrackState* SessionProcessor::findTrackState (int trackId) const
{
    for (auto& ts : trackStates)
        if (ts.trackId.load() == trackId && trackId != 0)
            return &ts;
    return nullptr;
}

//==============================================================================
void SessionProcessor::startRecording (RecordSource source, double maxSeconds)
{
    if (recordActive.load())
        return;

    recordSource = source;
    recordBuffer.setSize (2, (int) (sampleRate * maxSeconds), false, true);
    recordBuffer.clear();
    recordPosition.store (0);
    recordLevel.store (0.0f);
    recordActive.store (true, std::memory_order_release);
}

juce::AudioBuffer<float> SessionProcessor::stopRecording()
{
    recordActive.store (false);

    // Let a block that is still writing finish (blocks are a few milliseconds at most).
    for (int i = 0; i < 2000 && recordWriting.load(); ++i)
        std::this_thread::sleep_for (std::chrono::microseconds (100));

    const int length = recordPosition.load();
    juce::AudioBuffer<float> result (2, length);
    for (int ch = 0; ch < 2; ++ch)
        result.copyFrom (ch, 0, recordBuffer, ch, 0, length);

    recordBuffer.setSize (0, 0);
    return result;
}

//==============================================================================
void SessionProcessor::process (juce::AudioBuffer<float>& output, int numSamples,
                                const juce::AudioBuffer<float>* input)
{
    int done = 0;
    while (done < numSamples)
    {
        const int n = juce::jmin (maxBlockSize, numSamples - done);
        processChunk (output, done, n, input);
        done += n;
    }
}

void SessionProcessor::processChunk (juce::AudioBuffer<float>& output, int start, int numSamples,
                                     const juce::AudioBuffer<float>* input)
{
    for (int ch = 0; ch < output.getNumChannels(); ++ch)
        output.clear (ch, start, numSamples);

    if (auto* next = pending.load (std::memory_order_acquire); next != active)
        applySnapshot (next);

    if (active == nullptr)
        return;

    tempo = juce::jlimit (minTempo, maxTempo, active->tempo);

    for (auto& st : states)
        st.clearEvents();

    clockAtChunkStart = clockBeat;

    // Commands are applied at the start of the block (live notes land on sample 0).
    {
        const auto scope = commandFifo.read (commandFifo.getNumReady());
        for (int i = 0; i < scope.blockSize1; ++i) handleCommand (commands[(size_t) (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i) handleCommand (commands[(size_t) (scope.startIndex2 + i)]);
    }

    const double beatsPerSample = tempo / 60.0 / sampleRate;
    const double clock0 = clockBeat;
    const double song0 = songBeat;
    const bool isRunning = running.load();

    juce::AudioBuffer<float> outView (output.getArrayOfWritePointers(), juce::jmin (2, output.getNumChannels()),
                                      start, numSamples);

    mixTarget = &outView;
    for (size_t i = 0; i < active->tracks.size() && i < states.size(); ++i)
        renderTrack ((int) i, active->tracks[i], clock0, beatsPerSample, numSamples);
    mixTarget = nullptr;

    if (isRunning && active->metronome)
        renderMetronome (outView, song0, beatsPerSample, numSamples);
    else if (clickEnvelope > 0.0f)
        renderMetronome (outView, -1.0, beatsPerSample, numSamples); // let a click ring out

    // Input monitoring and sample recording.
    const bool hasInput = input != nullptr && input->getNumChannels() > 0 && input->getNumSamples() >= start + numSamples;

    if (hasInput && monitorInput.load())
        for (int ch = 0; ch < outView.getNumChannels(); ++ch)
            outView.addFrom (ch, 0, *input, juce::jmin (ch, input->getNumChannels() - 1), start, numSamples);

    recordWriting.store (true);
    if (recordActive.load (std::memory_order_acquire))
    {
        const int pos = recordPosition.load();
        const int toCopy = juce::jmin (numSamples, recordBuffer.getNumSamples() - pos);
        float peak = 0.0f;

        if (toCopy > 0)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                if (recordSource == RecordSource::input)
                {
                    if (hasInput)
                        recordBuffer.copyFrom (ch, pos, *input, juce::jmin (ch, input->getNumChannels() - 1), start, toCopy);
                }
                else
                {
                    recordBuffer.copyFrom (ch, pos, outView, juce::jmin (ch, outView.getNumChannels() - 1), 0, toCopy);
                }
                peak = juce::jmax (peak, recordBuffer.getMagnitude (ch, pos, toCopy));
            }
            recordPosition.store (pos + toCopy);
        }

        recordLevel.store (juce::jmax (peak, recordLevel.load() * 0.9f));

        if (pos + toCopy >= recordBuffer.getNumSamples())
            recordActive.store (false);
    }
    recordWriting.store (false);

    // Meter.
    float peak = 0.0f;
    for (int ch = 0; ch < outView.getNumChannels(); ++ch)
        peak = juce::jmax (peak, outView.getMagnitude (ch, 0, numSamples));
    masterLevelHold = juce::jmax (peak, masterLevelHold * 0.85f);
    masterLevel.store (masterLevelHold);

    // Advance time.
    clockBeat += numSamples * beatsPerSample;
    if (running.load())
        songBeat += numSamples * beatsPerSample;
    elapsedSeconds += numSamples / sampleRate;
    songBeatForUi.store (songBeat);
    elapsedForUi.store (elapsedSeconds);
    wasRunning = running.load();

    acknowledged.store (active->sequence);
}

//==============================================================================
void SessionProcessor::applySnapshot (const EngineSnapshot* next)
{
    active = next;
    if (next == nullptr)
        return;

    const int numTracks = juce::jmin ((int) next->tracks.size(), maxTracks);

    auto isInNext = [&] (int id)
    {
        for (int i = 0; i < numTracks; ++i)
            if (next->tracks[(size_t) i].id == id)
                return true;
        return false;
    };

    // Keep each track's play state with its track when tracks are reordered.
    for (int i = 0; i < maxTracks; ++i)
    {
        const int targetId = i < numTracks ? next->tracks[(size_t) i].id : 0;
        int found = -1;

        for (int j = i; j < maxTracks; ++j)
            if (states[(size_t) j].trackId == targetId && targetId != 0)
                { found = j; break; }

        if (found < 0)
        {
            for (int j = i; j < maxTracks; ++j)
                if (states[(size_t) j].trackId == 0 || ! isInNext (states[(size_t) j].trackId))
                    { found = j; break; }

            if (found < 0)
                found = i;

            std::swap (states[(size_t) i], states[(size_t) found]);
            auto& st = states[(size_t) i];
            st.trackId = targetId;
            st.playingSlot = -1;
            st.queuedSlot = noQueue;
            st.armedSlot = -1;
            st.numOffs = 0;
            st.liveHeld.fill (false);
            st.soundingCount.fill (0);
            st.gain.setCurrentAndTargetValue (i < numTracks ? next->tracks[(size_t) i].gain : 0.0f);
        }
        else if (found != i)
        {
            std::swap (states[(size_t) i], states[(size_t) found]);
        }

        trackStates[(size_t) i].trackId.store (targetId);
    }

    for (int i = 0; i < numTracks; ++i)
    {
        auto& st = states[(size_t) i];
        auto& track = next->tracks[(size_t) i];

        if (st.playingSlot >= 0 && track.clips[(size_t) st.playingSlot] == nullptr)
        {
            st.playingSlot = -1;
            st.numOffs = 0;
            if (track.instrument != nullptr)
                track.instrument->allNotesOff();
        }

        if (st.queuedSlot >= 0 && track.clips[(size_t) st.queuedSlot] == nullptr)
            st.queuedSlot = st.playingSlot >= 0 ? queuedStop : noQueue;

        if (st.armedSlot >= 0 && track.clips[(size_t) st.armedSlot] == nullptr)
            st.armedSlot = -1;
    }
}

double SessionProcessor::nextDownbeat (double beat) const
{
    return std::ceil (beat / beatsPerBar - 1.0e-9) * beatsPerBar;
}

void SessionProcessor::startTransport()
{
    running.store (true);
    songBeat = 0.0;
    songOrigin = clockBeat;

    bool anyArmed = false;
    const int numTracks = juce::jmin ((int) active->tracks.size(), maxTracks);

    for (int i = 0; i < numTracks; ++i)
        anyArmed = anyArmed || states[(size_t) i].armedSlot >= 0;

    // Nothing to resume: start the first scene that has clips.
    if (! anyArmed)
    {
        for (int scene = 0; scene < numScenes && ! anyArmed; ++scene)
            for (int i = 0; i < numTracks; ++i)
                if (active->tracks[(size_t) i].clips[(size_t) scene] != nullptr)
                {
                    anyArmed = true;
                    for (int t = 0; t < numTracks; ++t)
                        states[(size_t) t].armedSlot = active->tracks[(size_t) t].clips[(size_t) scene] != nullptr ? scene : -1;
                    break;
                }
    }

    for (int i = 0; i < numTracks; ++i)
    {
        auto& st = states[(size_t) i];
        st.playingSlot = st.armedSlot;
        st.clipStartBeat = 0.0;
        st.queuedSlot = noQueue;
    }
}

void SessionProcessor::stopTransport()
{
    running.store (false);

    const int numTracks = juce::jmin ((int) active->tracks.size(), maxTracks);
    for (int i = 0; i < numTracks; ++i)
    {
        auto& st = states[(size_t) i];
        st.armedSlot = st.queuedSlot >= 0 ? st.queuedSlot : st.playingSlot;
        endClipNotes (st, 0);
        st.playingSlot = -1;
        st.queuedSlot = noQueue;
    }
}

void SessionProcessor::handleCommand (const Command& c)
{
    const int numTracks = juce::jmin ((int) active->tracks.size(), maxTracks);
    const int index = active->indexOf (c.trackId);
    const bool isRunning = running.load();

    auto queueLaunch = [&] (int i, int slot)
    {
        auto& st = states[(size_t) i];
        st.queuedSlot = slot;
        st.queuedBeat = nextDownbeat (songBeat);
    };

    auto queueStop = [&] (int i)
    {
        auto& st = states[(size_t) i];
        if (st.playingSlot >= 0 || st.queuedSlot >= 0)
        {
            st.queuedSlot = st.playingSlot >= 0 ? queuedStop : noQueue;
            st.queuedBeat = nextDownbeat (songBeat);
        }
    };

    switch (c.type)
    {
        case Command::Type::play:
            if (! isRunning)
                startTransport();
            break;

        case Command::Type::stop:
            if (isRunning)
                stopTransport();
            break;

        case Command::Type::launchClip:
        {
            if (index < 0 || index >= numTracks || c.slot < 0 || c.slot >= numScenes)
                break;

            if (active->tracks[(size_t) index].clips[(size_t) c.slot] == nullptr)
            {
                if (isRunning) queueStop (index);
                break;
            }

            if (isRunning)
            {
                queueLaunch (index, c.slot);
            }
            else
            {
                for (int i = 0; i < numTracks; ++i)
                    states[(size_t) i].armedSlot = -1;
                states[(size_t) index].armedSlot = c.slot;
                startTransport();
            }
            break;
        }

        case Command::Type::stopClip:
            if (index < 0 || index >= numTracks)
                break;
            if (isRunning)
                queueStop (index);
            else
                states[(size_t) index].armedSlot = -1;
            break;

        case Command::Type::launchScene:
        {
            if (c.slot < 0 || c.slot >= numScenes)
                break;

            if (isRunning)
            {
                for (int i = 0; i < numTracks; ++i)
                {
                    if (active->tracks[(size_t) i].clips[(size_t) c.slot] != nullptr)
                        queueLaunch (i, c.slot);
                    else
                        queueStop (i);
                }
            }
            else
            {
                for (int i = 0; i < numTracks; ++i)
                    states[(size_t) i].armedSlot = active->tracks[(size_t) i].clips[(size_t) c.slot] != nullptr ? c.slot : -1;
                startTransport();
            }
            break;
        }

        case Command::Type::stopAllClips:
            for (int i = 0; i < numTracks; ++i)
            {
                if (isRunning)
                    queueStop (i);
                states[(size_t) i].armedSlot = -1;
            }
            break;

        case Command::Type::switchClipSeamless:
        {
            if (index < 0 || index >= numTracks || c.slot < 0 || c.slot >= numScenes)
                break;
            auto& st = states[(size_t) index];
            if (active->tracks[(size_t) index].clips[(size_t) c.slot] == nullptr)
                break;

            if (isRunning && st.playingSlot >= 0)
            {
                endClipNotes (st, 0);
                st.playingSlot = c.slot;
                st.queuedSlot = noQueue;
            }
            else if (isRunning)
            {
                queueLaunch (index, c.slot);
            }
            else
            {
                st.armedSlot = c.slot;
            }
            break;
        }

        case Command::Type::restartClip:
            if (index >= 0 && index < numTracks && isRunning)
            {
                auto& st = states[(size_t) index];
                if (st.playingSlot >= 0)
                {
                    endClipNotes (st, 0);
                    st.clipStartBeat = songBeat;
                }
            }
            break;

        case Command::Type::noteOn:
        {
            if (index < 0 || index >= numTracks || c.pitch < 0 || c.pitch > 127)
                break;
            auto& st = states[(size_t) index];
            st.liveHeld[(size_t) c.pitch] = true;
            st.liveVelocity[(size_t) c.pitch] = c.velocity;

            const double repeat = active->tracks[(size_t) index].repeatBeats;
            addEvent (st, 0, c.pitch, juce::jmax (0.01f, c.velocity), c.pitchOffset);
            pushCapture (st, c.pitch, juce::jmax (0.01f, c.velocity), c.pitchOffset, clockBeat);

            if (repeat > 0.0 && st.numOffs < (int) st.offs.size())
            {
                // First hit of a repeat: gated like the repeats that follow.
                st.offs[(size_t) st.numOffs++] = { c.pitch, clockBeat + repeat * 0.5, false };
                pushCapture (st, c.pitch, 0.0f, 0.0f, clockBeat + repeat * 0.5);
            }
            break;
        }

        case Command::Type::noteOff:
        {
            if (index < 0 || index >= numTracks || c.pitch < 0 || c.pitch > 127)
                break;
            auto& st = states[(size_t) index];
            if (! st.liveHeld[(size_t) c.pitch])
                break;
            st.liveHeld[(size_t) c.pitch] = false;

            if (active->tracks[(size_t) index].repeatBeats <= 0.0)
            {
                addEvent (st, 0, c.pitch, 0.0f, 0.0f);
                pushCapture (st, c.pitch, 0.0f, 0.0f, clockBeat);
            }
            break;
        }

        case Command::Type::allLiveNotesOff:
            for (int i = 0; i < numTracks; ++i)
            {
                if (c.trackId != 0 && i != index)
                    continue;
                auto& st = states[(size_t) i];
                for (int p = 0; p < 128; ++p)
                    if (st.liveHeld[(size_t) p])
                    {
                        st.liveHeld[(size_t) p] = false;
                        addEvent (st, 0, p, 0.0f, 0.0f);
                    }
            }
            break;
    }
}

//==============================================================================
void SessionProcessor::addEvent (PlayState& st, int offset, int pitch, float velocity, float pitchOffset)
{
    if (st.numEvents >= (int) st.events.size())
        return;

    NoteEvent e;
    e.sampleOffset = offset;
    e.pitch = pitch;
    e.velocity = velocity;
    e.pitchOffset = pitchOffset;
    st.events[(size_t) st.numEvents++] = e;
}

void SessionProcessor::endClipNotes (PlayState& st, int offset)
{
    for (int k = 0; k < st.numOffs;)
    {
        if (st.offs[(size_t) k].fromClip)
        {
            addEvent (st, offset, st.offs[(size_t) k].pitch, 0.0f, 0.0f);
            st.offs[(size_t) k] = st.offs[(size_t) --st.numOffs];
        }
        else
        {
            ++k;
        }
    }
}

double SessionProcessor::clipPositionAt (const PlayState& st, const Clip& clip, double beat) const
{
    if (clip.loopLength <= 0.0 || beat < st.clipStartBeat)
        return clip.loopStart;
    return clip.loopStart + std::fmod (beat - st.clipStartBeat, clip.loopLength);
}

void SessionProcessor::pushCapture (const PlayState& st, int pitch, float velocity, float pitchOffset,
                                    double atClockBeat)
{
    const auto scope = captureFifo.write (1);
    if (scope.blockSize1 + scope.blockSize2 == 0)
        return;

    CaptureEvent e;
    e.trackId = st.trackId;
    e.pitch = pitch;
    e.velocity = velocity;
    e.pitchOffset = pitchOffset;
    e.transportRunning = running.load();
    e.songBeat = atClockBeat - songOrigin;
    e.timeSeconds = elapsedSeconds + (atClockBeat - clockAtChunkStart) * 60.0 / tempo;
    e.playingSlot = e.transportRunning ? st.playingSlot : -1;

    if (e.playingSlot >= 0 && active != nullptr)
    {
        const int index = active->indexOf (st.trackId);
        if (index >= 0)
            if (auto& clip = active->tracks[(size_t) index].clips[(size_t) e.playingSlot])
                e.clipBeat = clipPositionAt (st, *clip, e.songBeat);
    }

    captures[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)] = e;
}

void SessionProcessor::emitClipNotes (PlayState& st, const Clip& clip, double song0, double song1,
                                      double clock0, double beatsPerSample, int numSamples)
{
    const double length = clip.loopLength;
    if (length <= 0.0 || song1 <= song0)
        return;

    double rel0 = song0 - st.clipStartBeat;
    const double rel1 = song1 - st.clipStartBeat;
    if (rel1 <= 0.0)
        return;
    rel0 = juce::jmax (0.0, rel0);

    auto& notes = clip.notes;
    const double loopEnd = clip.loopEnd();

    for (double cycle = std::floor (rel0 / length); cycle * length < rel1; cycle += 1.0)
    {
        const double a = juce::jmax (rel0, cycle * length);
        const double b = juce::jmin (rel1, (cycle + 1.0) * length);
        if (b <= a)
            continue;

        const double localA = clip.loopStart + (a - cycle * length);
        const double localB = clip.loopStart + (b - cycle * length);

        auto it = std::lower_bound (notes.begin(), notes.end(), localA,
                                    [] (const Note& n, double v) { return n.start < v; });

        for (; it != notes.end() && it->start < localB; ++it)
        {
            if (it->start >= loopEnd)
                break;

            const double songOn = st.clipStartBeat + cycle * length + (it->start - clip.loopStart);
            const double clockOn = songOn + songOrigin;
            const int offset = sampleOffsetOf (clockOn, clock0, beatsPerSample, numSamples);
            const double noteLength = juce::jmax (1.0 / 128.0, juce::jmin (it->length, loopEnd - it->start));

            // A note retriggering a pitch that is still held by the clip ends the old one first.
            for (int k = 0; k < st.numOffs; ++k)
                if (st.offs[(size_t) k].fromClip && st.offs[(size_t) k].pitch == it->pitch)
                {
                    addEvent (st, offset, it->pitch, 0.0f, 0.0f);
                    st.offs[(size_t) k] = st.offs[(size_t) --st.numOffs];
                    break;
                }

            addEvent (st, offset, it->pitch, juce::jmax (0.01f, it->velocity), it->pitchOffset);

            if (st.numOffs < (int) st.offs.size())
                st.offs[(size_t) st.numOffs++] = { it->pitch, clockOn + noteLength, true };
        }
    }
}

static const float* applyLanes (const std::vector<float>& base, int device, int pad, const Clip* clip, double position,
                         std::array<float, maxDeviceParams>& scratch)
{
    if (clip == nullptr || clip->automation.empty())
        return base.data();

    bool copied = false;
    for (auto& lane : clip->automation)
    {
        if (lane.target.device != device || (device == ParamTarget::pad && lane.target.padIndex != pad))
            continue;
        if (lane.target.paramIndex < 0 || lane.target.paramIndex >= (int) base.size() || lane.points.empty())
            continue;

        if (! copied)
        {
            std::copy (base.begin(), base.begin() + juce::jmin ((int) base.size(), maxDeviceParams), scratch.begin());
            copied = true;
        }
        scratch[(size_t) lane.target.paramIndex] = lane.valueAt (position);
    }

    return copied ? scratch.data() : base.data();
}

void SessionProcessor::renderTrack (int index, const TrackSnapshot& track, double clock0, double beatsPerSample,
                                    int numSamples)
{
    auto& st = states[(size_t) index];
    auto& ui = trackStates[(size_t) index];

    // Windows are shifted back by a tiny amount so an event exactly on a block boundary is
    // always handled by the later block, at offset 0, whatever the rounding of the clock.
    const double clock1 = clock0 + numSamples * beatsPerSample - beatEpsilon;
    const double song0 = songBeat - beatEpsilon;
    const double song1 = songBeat + numSamples * beatsPerSample - beatEpsilon;
    const bool isRunning = running.load();

    auto offsetOf = [&] (double clock) { return sampleOffsetOf (clock, clock0, beatsPerSample, numSamples); };

    // 1. Clip playback, including a queued launch/stop landing in this block.
    if (isRunning)
    {
        double segmentStart = song0;

        if (st.queuedSlot != noQueue && st.queuedBeat < song1)
        {
            const double switchBeat = juce::jmax (st.queuedBeat, song0);

            if (st.playingSlot >= 0)
                if (auto* clip = track.clips[(size_t) st.playingSlot].get())
                    emitClipNotes (st, *clip, song0, switchBeat, clock0, beatsPerSample, numSamples);

            endClipNotes (st, offsetOf (switchBeat + songOrigin));

            if (st.queuedSlot >= 0 && track.clips[(size_t) st.queuedSlot] != nullptr)
            {
                st.playingSlot = st.queuedSlot;
                st.clipStartBeat = switchBeat;
            }
            else
            {
                st.playingSlot = -1;
            }

            st.queuedSlot = noQueue;
            segmentStart = switchBeat;
        }

        if (st.playingSlot >= 0)
            if (auto* clip = track.clips[(size_t) st.playingSlot].get())
                emitClipNotes (st, *clip, segmentStart, song1, clock0, beatsPerSample, numSamples);
    }

    // 2. Repeat (PAD-7): retrigger held live notes on the grid.
    if (track.repeatBeats > 0.0)
    {
        bool anyHeld = false;
        for (auto held : st.liveHeld)
            anyHeld = anyHeld || held;

        if (anyHeld)
        {
            const double rate = track.repeatBeats;
            const double gridOrigin = isRunning ? songOrigin : 0.0;
            const double start = clock0 - beatEpsilon - gridOrigin;
            const double end = clock1 - gridOrigin;

            for (double g = std::ceil (start / rate - 1.0e-9) * rate; g < end; g += rate)
            {
                const double atClock = g + gridOrigin;
                // Skip a grid point right after the initial press (it already sounded).
                if (std::abs (atClock - clockAtChunkStart) < 1.0e-6 && st.numEvents > 0)
                    continue;

                for (int p = 0; p < 128; ++p)
                {
                    if (! st.liveHeld[(size_t) p])
                        continue;

                    const int offset = offsetOf (atClock);
                    addEvent (st, offset, p, 0.0f, 0.0f); // end the previous repeat if still sounding
                    addEvent (st, offset, p, juce::jmax (0.01f, st.liveVelocity[(size_t) p]), 0.0f);
                    pushCapture (st, p, st.liveVelocity[(size_t) p], 0.0f, atClock);
                    pushCapture (st, p, 0.0f, 0.0f, atClock + rate * 0.5);

                    for (int k = 0; k < st.numOffs; ++k)
                        if (! st.offs[(size_t) k].fromClip && st.offs[(size_t) k].pitch == p)
                        {
                            st.offs[(size_t) k] = st.offs[(size_t) --st.numOffs];
                            break;
                        }

                    if (st.numOffs < (int) st.offs.size())
                        st.offs[(size_t) st.numOffs++] = { p, atClock + rate * 0.5, false };
                }
            }
        }
    }

    // 3. Note-offs that fall inside this block.
    for (int k = 0; k < st.numOffs;)
    {
        if (st.offs[(size_t) k].clockBeat < clock1)
        {
            addEvent (st, offsetOf (st.offs[(size_t) k].clockBeat), st.offs[(size_t) k].pitch, 0.0f, 0.0f);
            st.offs[(size_t) k] = st.offs[(size_t) --st.numOffs];
        }
        else
        {
            ++k;
        }
    }

    // Sort by offset; at equal offsets note-offs go first so retriggers work.
    std::stable_sort (st.events.begin(), st.events.begin() + st.numEvents, [] (const NoteEvent& a, const NoteEvent& b)
    {
        if (a.sampleOffset != b.sampleOffset) return a.sampleOffset < b.sampleOffset;
        return ! a.isNoteOn() && b.isNoteOn();
    });

    for (int e = 0; e < st.numEvents; ++e)
    {
        auto& ev = st.events[(size_t) e];
        auto& count = st.soundingCount[(size_t) juce::jlimit (0, 127, ev.pitch)];
        if (ev.isNoteOn()) count = (juce::uint8) juce::jmin (255, count + 1);
        else if (count > 0) --count;
    }

    juce::uint64 low = 0, high = 0;
    for (int p = 0; p < 128; ++p)
        if (st.soundingCount[(size_t) p] > 0)
            (p < 64 ? low : high) |= (juce::uint64) 1 << (p & 63);
    ui.soundingLow.store (low);
    ui.soundingHigh.store (high);

    // 4. Devices.
    const Clip* playingClip = (isRunning && st.playingSlot >= 0) ? track.clips[(size_t) st.playingSlot].get() : nullptr;
    const double clipPosition = playingClip != nullptr ? clipPositionAt (st, *playingClip, song0) : 0.0;

    dryBuffer.clear (0, numSamples);
    sendBuffer.clear (0, numSamples);

    juce::AudioBuffer<float> dry (dryBuffer.getArrayOfWritePointers(), 2, 0, numSamples);
    juce::AudioBuffer<float> send (sendBuffer.getArrayOfWritePointers(), 2, 0, numSamples);

    if (track.instrument != nullptr)
    {
        auto* instrument = track.instrument.get();

        instrument->setParameters (applyLanes (track.instrumentValues, ParamTarget::instrument, 0, playingClip,
                                               clipPosition, scratchValues),
                                   (int) track.instrumentValues.size());

        if (track.instrumentType == InstrumentType::drumKit)
        {
            for (int p = 0; p < numDrumPads; ++p)
            {
                auto& values = track.padValues[(size_t) p];
                if (! values.empty())
                    instrument->setPadParameters (p, applyLanes (values, ParamTarget::pad, p, playingClip,
                                                                 clipPosition, scratchValues),
                                                  (int) values.size());
                instrument->setSample (p, track.padSamples[(size_t) p].get());
            }
        }
        else if (track.instrumentType == InstrumentType::sampler)
        {
            instrument->setSample (0, track.samplerSample.get());
        }

        instrument->process (dry, send, st.events.data(), st.numEvents, numSamples);
    }

    for (int slot = 0; slot < 2; ++slot)
        if (auto* fx = track.effects[(size_t) slot].get())
        {
            auto& values = track.effectValues[(size_t) slot];
            fx->setTempo (tempo);
            fx->setParameters (applyLanes (values, slot == 0 ? ParamTarget::effect1 : ParamTarget::effect2, 0,
                                           playingClip, clipPosition, scratchValues),
                               (int) values.size());
        }

    if (track.instrumentType == InstrumentType::drumKit)
    {
        // Pad FX is a send effect fed by each pad's send amount; Kit FX is an insert (DEV-21).
        if (auto* padFx = track.effects[0].get())
        {
            padFx->process (send, numSamples);
            for (int ch = 0; ch < 2; ++ch)
                dry.addFrom (ch, 0, send, ch, 0, numSamples);
        }
        if (auto* kitFx = track.effects[1].get())
            kitFx->process (dry, numSamples);
    }
    else
    {
        for (auto& fx : track.effects)
            if (fx != nullptr)
                fx->process (dry, numSamples);
    }

    // 5. Mixer.
    st.gain.setTargetValue (track.gain);
    float peak = 0.0f;
    float* left = dry.getWritePointer (0);
    float* right = dry.getWritePointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = st.gain.getNextValue();
        left[i] *= g;
        right[i] *= g;
        peak = juce::jmax (peak, std::abs (left[i]), std::abs (right[i]));
    }

    st.levelHold = juce::jmax (peak, st.levelHold * 0.85f);
    ui.level.store (st.levelHold);

    for (int ch = 0; ch < juce::jmin (2, mixTarget->getNumChannels()); ++ch)
        mixTarget->addFrom (ch, 0, dry, ch, 0, numSamples);

    // UI state.
    ui.playingSlot.store (isRunning ? st.playingSlot : -1);
    ui.queuedSlot.store (isRunning ? st.queuedSlot : noQueue);
    ui.clipBeat.store (playingClip != nullptr ? clipPositionAt (st, *playingClip, song1) : 0.0);
}

void SessionProcessor::renderMetronome (juce::AudioBuffer<float>& out, double song0, double beatsPerSample,
                                        int numSamples)
{
    int nextClick = -1;
    int nextBeat = 0;

    if (song0 >= 0.0)
    {
        const double first = std::ceil (song0 - beatEpsilon);
        const double offset = std::ceil ((first - song0) / beatsPerSample - 1.0e-6);
        if (offset < numSamples)
        {
            nextClick = juce::jmax (0, (int) offset);
            nextBeat = (int) first;
        }
    }

    const float twoPi = juce::MathConstants<float>::twoPi;

    for (int i = 0; i < numSamples; ++i)
    {
        if (i == nextClick)
        {
            const bool downbeat = nextBeat % beatsPerBar == 0;
            clickIncrement = (downbeat ? 1600.0f : 1000.0f) / (float) sampleRate;
            clickGain = downbeat ? 0.35f : 0.25f;
            clickEnvelope = 1.0f;
            clickPhase = 0.0f;

            ++nextBeat;
            const double following = std::ceil ((nextBeat - song0) / beatsPerSample - 1.0e-6);
            nextClick = following < numSamples ? (int) following : -1;
        }

        if (clickEnvelope > 1.0e-4f)
        {
            const float s = std::sin (twoPi * clickPhase) * clickEnvelope * clickGain;
            clickPhase += clickIncrement;
            if (clickPhase >= 1.0f) clickPhase -= 1.0f;
            clickEnvelope *= clickDecay;
            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                out.addSample (ch, i, s);
        }
        else
        {
            clickEnvelope = 0.0f;
        }
    }
}

} // namespace rb
