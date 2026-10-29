namespace rb
{

static int roundUpToPowerOfTwoBars (int bars, int maxBars)
{
    int b = 1;
    while (b < bars && b < maxBars)
        b *= 2;
    return juce::jmin (b, maxBars);
}

void CaptureBuffer::add (const CaptureEvent& e)
{
    auto& notes = tracks[e.trackId];

    if (e.velocity > 0.0f)
    {
        // A new phrase after a long silence replaces the uncaptured one (CAP-7, timeout off).
        if (! notes.empty() && e.timeSeconds - lastEventSeconds > 8.0)
            notes.erase (std::remove_if (notes.begin(), notes.end(), [] (const PlayedNote& n) { return n.slot < 0; }),
                         notes.end());

        PlayedNote n;
        n.pitch = e.pitch;
        n.velocity = e.velocity;
        n.pitchOffset = e.pitchOffset;
        n.transportRunning = e.transportRunning;
        n.onBeat = n.offBeat = e.songBeat;
        n.onSeconds = n.offSeconds = e.timeSeconds;
        n.slot = e.playingSlot;
        n.clipOn = n.clipOff = e.clipBeat;
        notes.push_back (n);

        if (notes.size() > maxNotesPerTrack)
            notes.erase (notes.begin());
    }
    else
    {
        for (auto it = notes.rbegin(); it != notes.rend(); ++it)
            if (it->open && it->pitch == e.pitch)
            {
                it->open = false;
                it->offBeat = e.songBeat;
                it->offSeconds = e.timeSeconds;
                it->clipOff = e.clipBeat;
                break;
            }
    }

    lastEventSeconds = juce::jmax (lastEventSeconds, e.timeSeconds);
}

void CaptureBuffer::update (double nowSeconds, bool timeoutEnabled)
{
    if (! timeoutEnabled || nowSeconds - lastEventSeconds < timeoutSeconds)
        return;

    for (auto& [id, notes] : tracks)
    {
        const bool anyOpen = std::any_of (notes.begin(), notes.end(), [] (const PlayedNote& n) { return n.open; });
        if (! anyOpen)
            notes.erase (std::remove_if (notes.begin(), notes.end(), [] (const PlayedNote& n) { return n.slot < 0; }),
                         notes.end());
    }
}

void CaptureBuffer::clear (int trackId)
{
    if (auto it = tracks.find (trackId); it != tracks.end())
        it->second.erase (std::remove_if (it->second.begin(), it->second.end(),
                                          [] (const PlayedNote& n) { return n.slot < 0; }),
                          it->second.end());
}

void CaptureBuffer::clearAll()
{
    tracks.clear();
}

bool CaptureBuffer::hasFreeNotes (int trackId) const
{
    if (auto it = tracks.find (trackId); it != tracks.end())
        for (auto& n : it->second)
            if (n.slot < 0)
                return true;
    return false;
}

const std::vector<CaptureBuffer::PlayedNote>& CaptureBuffer::getNotes (int trackId) const
{
    static const std::vector<PlayedNote> empty;
    if (auto it = tracks.find (trackId); it != tracks.end())
        return it->second;
    return empty;
}

CaptureBuffer::FreeCapture CaptureBuffer::captureFree (int trackId, double tempo, bool detectTempo, int minBars) const
{
    FreeCapture result;

    std::vector<PlayedNote> free;
    for (auto& n : getNotes (trackId))
        if (n.slot < 0)
            free.push_back (n);

    if (free.empty())
        return result;

    // Use the notes played in the most recent transport state.
    const bool running = free.back().transportRunning;
    free.erase (std::remove_if (free.begin(), free.end(), [&] (const PlayedNote& n) { return n.transportRunning != running; }),
                free.end());

    std::vector<Note> notes;

    if (! running)
    {
        std::vector<double> onsets;
        for (auto& n : free)
            onsets.push_back (n.onSeconds);

        int bars = 0;
        if (detectTempo && onsets.size() >= 2)
        {
            const auto estimate = estimateTempo (onsets);
            tempo = estimate.bpm;
            bars = estimate.bars;
            result.tempo = tempo;
        }

        const double t0 = *std::min_element (onsets.begin(), onsets.end());
        const double beatsPerSecond = tempo / 60.0;

        double lastOnset = 0.0;
        for (auto& n : free)
            lastOnset = juce::jmax (lastOnset, (n.onSeconds - t0) * beatsPerSecond);

        if (bars == 0)
            bars = roundUpToPowerOfTwoBars ((int) std::floor (lastOnset / beatsPerBar) + 1, 8);

        bars = juce::jmax (bars, juce::jmin (minBars, maxClipBars));
        const double length = bars * (double) beatsPerBar;

        for (auto& n : free)
        {
            Note note;
            note.start = (n.onSeconds - t0) * beatsPerSecond;
            if (note.start >= length)
                continue;
            note.length = n.open ? 0.25 : juce::jmax (1.0 / 64.0, (n.offSeconds - n.onSeconds) * beatsPerSecond);
            note.pitch = n.pitch;
            note.velocity = n.velocity;
            note.pitchOffset = n.pitchOffset;
            notes.push_back (note);
        }

        result.clip = makeClip (std::move (notes), length);
        return result;
    }

    double firstOnset = free.front().onBeat, lastOnset = free.front().onBeat;
    for (auto& n : free)
    {
        firstOnset = juce::jmin (firstOnset, n.onBeat);
        lastOnset = juce::jmax (lastOnset, n.onBeat);
    }

    const double startBeat = std::floor (firstOnset / beatsPerBar + 1.0e-9) * beatsPerBar;
    int bars = (int) std::floor ((lastOnset - startBeat) / beatsPerBar) + 1;
    bars = roundUpToPowerOfTwoBars (juce::jmax (bars, minBars), maxClipBars);
    const double length = bars * (double) beatsPerBar;

    for (auto& n : free)
    {
        Note note;
        note.start = n.onBeat - startBeat;
        if (note.start >= length)
            continue;
        note.length = n.open ? 0.25 : juce::jmax (1.0 / 64.0, n.offBeat - n.onBeat);
        note.pitch = n.pitch;
        note.velocity = n.velocity;
        note.pitchOffset = n.pitchOffset;
        notes.push_back (note);
    }

    result.clip = makeClip (std::move (notes), length);
    return result;
}

std::vector<Note> CaptureBuffer::pendingForClip (int trackId, int slot, const Clip& clip) const
{
    std::vector<Note> notes;
    for (auto& n : getNotes (trackId))
    {
        if (n.slot != slot)
            continue;

        Note note;
        note.start = n.clipOn;
        double length = n.open ? 0.25 : n.clipOff - n.clipOn;
        if (length <= 0.0 && ! n.open)
            length += clip.loopLength;
        note.length = juce::jlimit (1.0 / 64.0, clip.loopLength, length);
        note.pitch = n.pitch;
        note.velocity = n.velocity;
        note.pitchOffset = n.pitchOffset;
        notes.push_back (note);
    }
    return notes;
}

void CaptureBuffer::clearPendingForClip (int trackId, int slot)
{
    if (auto it = tracks.find (trackId); it != tracks.end())
        it->second.erase (std::remove_if (it->second.begin(), it->second.end(),
                                          [&] (const PlayedNote& n) { return n.slot == slot; }),
                          it->second.end());
}

} // namespace rb
