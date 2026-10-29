namespace rb
{

//==============================================================================
Track makeTrack (Set& set, const TrackPreset& preset)
{
    Track t;
    t.id = set.nextTrackId++;

    // Pick the first colour no other track uses.
    std::array<bool, 16> used {};
    for (auto& other : set.tracks)
        used[(size_t) (other.colour & 15)] = true;
    for (int c = 0; c < 16; ++c)
        if (! used[(size_t) c]) { t.colour = c; break; }

    for (auto& fx : t.effects)
        fx.values = getEffectSpec (EffectType::none).defaults();
    for (auto& pad : t.pads)
        pad.values = getPadSpec().defaults();

    applyPreset (t, preset);
    t.presetModified = false;
    return t;
}

bool addTrack (Set& set, const TrackPreset& preset)
{
    if ((int) set.tracks.size() >= maxTracks)
        return false;
    set.tracks.push_back (makeTrack (set, preset));
    return true;
}

int duplicateTrack (Set& set, int trackId)
{
    const int index = set.indexOfTrack (trackId);
    if (index < 0 || (int) set.tracks.size() >= maxTracks)
        return 0;

    Track copy = set.tracks[(size_t) index];
    copy.id = set.nextTrackId++;
    copy.solo = false;
    set.tracks.insert (set.tracks.begin() + index + 1, copy);
    return copy.id;
}

void deleteTrack (Set& set, int trackId)
{
    const int index = set.indexOfTrack (trackId);
    if (index >= 0)
        set.tracks.erase (set.tracks.begin() + index);
}

void moveTrack (Set& set, int trackId, int newIndex)
{
    const int index = set.indexOfTrack (trackId);
    if (index < 0)
        return;
    newIndex = juce::jlimit (0, (int) set.tracks.size() - 1, newIndex);
    if (newIndex == index)
        return;

    Track t = std::move (set.tracks[(size_t) index]);
    set.tracks.erase (set.tracks.begin() + index);
    set.tracks.insert (set.tracks.begin() + newIndex, std::move (t));
}

//==============================================================================
void setClip (Set& set, int trackId, int slot, ClipPtr clip)
{
    if (auto* t = set.findTrack (trackId))
        if (slot >= 0 && slot < numScenes)
            t->slots[(size_t) slot] = std::move (clip);
}

void moveClip (Set& set, int fromTrackId, int fromSlot, int toTrackId, int toSlot)
{
    auto* from = set.findTrack (fromTrackId);
    auto* to = set.findTrack (toTrackId);
    if (from == nullptr || to == nullptr || fromSlot < 0 || toSlot < 0 || fromSlot >= numScenes || toSlot >= numScenes)
        return;
    std::swap (from->slots[(size_t) fromSlot], to->slots[(size_t) toSlot]);
}

int duplicateClip (Set& set, int trackId, int slot)
{
    auto* t = set.findTrack (trackId);
    if (t == nullptr || slot < 0 || slot >= numScenes || t->slots[(size_t) slot] == nullptr)
        return -1;

    // Next free slot below; failing that, the first free slot above.
    for (int s = slot + 1; s < numScenes; ++s)
        if (t->slots[(size_t) s] == nullptr) { t->slots[(size_t) s] = t->slots[(size_t) slot]; return s; }
    for (int s = 0; s < slot; ++s)
        if (t->slots[(size_t) s] == nullptr) { t->slots[(size_t) s] = t->slots[(size_t) slot]; return s; }
    return -1;
}

void deleteClip (Set& set, int trackId, int slot)
{
    setClip (set, trackId, slot, nullptr);
}

bool sceneIsEmpty (const Set& set, int scene)
{
    for (auto& t : set.tracks)
        if (t.slots[(size_t) scene] != nullptr)
            return false;
    return true;
}

void moveScene (Set& set, int fromScene, int toScene)
{
    if (fromScene < 0 || fromScene >= numScenes || toScene < 0 || toScene >= numScenes || fromScene == toScene)
        return;

    for (auto& t : set.tracks)
    {
        auto clip = t.slots[(size_t) fromScene];
        if (fromScene < toScene)
            for (int s = fromScene; s < toScene; ++s) t.slots[(size_t) s] = t.slots[(size_t) s + 1];
        else
            for (int s = fromScene; s > toScene; --s) t.slots[(size_t) s] = t.slots[(size_t) s - 1];
        t.slots[(size_t) toScene] = clip;
    }
}

bool duplicateScene (Set& set, int scene)
{
    if (scene < 0 || scene >= numScenes - 1 || ! sceneIsEmpty (set, numScenes - 1))
        return false;

    for (auto& t : set.tracks)
    {
        for (int s = numScenes - 1; s > scene + 1; --s)
            t.slots[(size_t) s] = t.slots[(size_t) s - 1];
        t.slots[(size_t) scene + 1] = t.slots[(size_t) scene];
    }
    return true;
}

void deleteScene (Set& set, int scene)
{
    if (scene < 0 || scene >= numScenes)
        return;

    for (auto& t : set.tracks)
    {
        for (int s = scene; s < numScenes - 1; ++s)
            t.slots[(size_t) s] = t.slots[(size_t) s + 1];
        t.slots[(size_t) numScenes - 1] = nullptr;
    }
}

//==============================================================================
ClipPtr makeEmptyClip (int bars)
{
    auto clip = std::make_shared<Clip>();
    clip->lengthBars = juce::jlimit (1, maxClipBars, bars);
    clip->loopStart = 0.0;
    clip->loopLength = clip->lengthBeats();
    return clip;
}

ClipPtr makeClip (std::vector<Note> notes, double lengthBeats)
{
    const int bars = juce::jlimit (1, maxClipBars, (int) std::ceil (lengthBeats / beatsPerBar - 1.0e-6));
    auto clip = std::make_shared<Clip>();
    clip->lengthBars = bars;
    clip->loopLength = clip->lengthBeats();
    clip->notes = std::move (notes);
    clip->sortNotes();
    return clip;
}

// Sorts notes while keeping track of which ones are selected.
static ClipEdit finish (Clip clip, std::vector<bool> selected)
{
    std::vector<std::pair<Note, bool>> tagged;
    tagged.reserve (clip.notes.size());
    for (size_t i = 0; i < clip.notes.size(); ++i)
        tagged.emplace_back (clip.notes[i], i < selected.size() && selected[i]);

    std::stable_sort (tagged.begin(), tagged.end(), [] (auto& a, auto& b)
    {
        if (! juce::exactlyEqual (a.first.start, b.first.start)) return a.first.start < b.first.start;
        return a.first.pitch < b.first.pitch;
    });

    ClipEdit result;
    clip.notes.clear();
    for (size_t i = 0; i < tagged.size(); ++i)
    {
        clip.notes.push_back (tagged[i].first);
        if (tagged[i].second)
            result.selection.push_back ((int) i);
    }
    result.clip = std::make_shared<Clip> (std::move (clip));
    return result;
}

static std::vector<bool> maskOf (const Clip& clip, const Selection& selection)
{
    std::vector<bool> mask (clip.notes.size(), false);
    for (auto i : selection)
        if (i >= 0 && i < (int) mask.size())
            mask[(size_t) i] = true;
    return mask;
}

ClipEdit addNote (const Clip& clip, const Note& note)
{
    Clip copy = clip;
    copy.notes.push_back (note);
    std::vector<bool> mask (copy.notes.size(), false);
    mask.back() = true;
    return finish (std::move (copy), std::move (mask));
}

ClipEdit deleteNotes (const Clip& clip, const Selection& selection)
{
    auto mask = maskOf (clip, selection);
    Clip copy = clip;
    copy.notes.clear();
    for (size_t i = 0; i < clip.notes.size(); ++i)
        if (! mask[i])
            copy.notes.push_back (clip.notes[i]);
    return finish (std::move (copy), {});
}

ClipEdit moveNotes (const Clip& clip, const Selection& selection, double deltaBeats, int deltaPitch)
{
    auto mask = maskOf (clip, selection);
    Clip copy = clip;
    for (size_t i = 0; i < copy.notes.size(); ++i)
        if (mask[i])
        {
            auto& n = copy.notes[i];
            n.start = juce::jlimit (0.0, copy.lengthBeats() - 1.0 / 128.0, n.start + deltaBeats);
            n.pitch = juce::jlimit (0, 127, n.pitch + deltaPitch);
            n.unquantizedStart = -1.0;
        }
    return finish (std::move (copy), std::move (mask));
}

ClipEdit resizeNotes (const Clip& clip, const Selection& selection, double deltaBeats, double minLength)
{
    auto mask = maskOf (clip, selection);
    Clip copy = clip;
    for (size_t i = 0; i < copy.notes.size(); ++i)
        if (mask[i])
            copy.notes[i].length = juce::jlimit (minLength, copy.lengthBeats(), copy.notes[i].length + deltaBeats);
    return finish (std::move (copy), std::move (mask));
}

ClipEdit setNoteLength (const Clip& clip, int index, double length)
{
    Clip copy = clip;
    std::vector<bool> mask (copy.notes.size(), false);
    if (index >= 0 && index < (int) copy.notes.size())
    {
        copy.notes[(size_t) index].length = juce::jlimit (1.0 / 128.0, copy.lengthBeats(), length);
        mask[(size_t) index] = true;
    }
    return finish (std::move (copy), std::move (mask));
}

ClipEdit transposeNotes (const Clip& clip, const Selection& selection, int steps, bool isDrum, bool inKey,
                         int root, int scale)
{
    auto mask = maskOf (clip, selection);
    Clip copy = clip;
    for (size_t i = 0; i < copy.notes.size(); ++i)
        if (mask[i])
        {
            auto& n = copy.notes[i];
            if (isDrum)
                n.pitch = juce::jlimit (drumBaseNote, drumBaseNote + numDrumPads - 1, n.pitch + steps);
            else
                n.pitch = transposeInScale (n.pitch, steps, inKey, root, scale);
        }
    return finish (std::move (copy), std::move (mask));
}

ClipEdit transposeOctave (const Clip& clip, const Selection& selection, int octaves)
{
    return moveNotes (clip, selection, 0.0, octaves * 12);
}

ClipEdit quantizeNotes (const Clip& clip, const Selection& selection, double grid, float strength)
{
    auto mask = maskOf (clip, selection);
    Clip copy = clip;
    for (size_t i = 0; i < copy.notes.size(); ++i)
    {
        if (! mask[i])
            continue;
        auto& n = copy.notes[i];

        if (grid <= 0.0)
        {
            if (n.unquantizedStart >= 0.0)
                n.start = n.unquantizedStart;
            n.unquantizedStart = -1.0;
            continue;
        }

        const double original = n.unquantizedStart >= 0.0 ? n.unquantizedStart : n.start;
        const double target = std::round (original / grid) * grid;
        n.unquantizedStart = original;
        n.start = juce::jlimit (0.0, copy.lengthBeats() - 1.0 / 128.0,
                                original + (target - original) * juce::jlimit (0.0f, 1.0f, strength));
    }
    return finish (std::move (copy), std::move (mask));
}

ClipEdit setVelocities (const Clip& clip, const Selection& selection, const std::vector<float>& velocities)
{
    auto mask = maskOf (clip, selection);
    Clip copy = clip;
    for (size_t k = 0; k < selection.size() && k < velocities.size(); ++k)
        if (selection[k] >= 0 && selection[k] < (int) copy.notes.size())
            copy.notes[(size_t) selection[k]].velocity = juce::jlimit (0.01f, 1.0f, velocities[k]);
    return finish (std::move (copy), std::move (mask));
}

ClipEdit setPitchOffsets (const Clip& clip, const Selection& selection, const std::vector<float>& offsets)
{
    auto mask = maskOf (clip, selection);
    Clip copy = clip;
    for (size_t k = 0; k < selection.size() && k < offsets.size(); ++k)
        if (selection[k] >= 0 && selection[k] < (int) copy.notes.size())
            copy.notes[(size_t) selection[k]].pitchOffset = juce::jlimit (-24.0f, 24.0f, offsets[k]);
    return finish (std::move (copy), std::move (mask));
}

Selection selectAll (const Clip& clip)
{
    Selection s;
    for (size_t i = 0; i < clip.notes.size(); ++i)
        s.push_back ((int) i);
    return s;
}

Selection selectInRect (const Clip& clip, double beat0, double beat1, int pitch0, int pitch1)
{
    if (beat1 < beat0) std::swap (beat0, beat1);
    if (pitch1 < pitch0) std::swap (pitch0, pitch1);

    Selection s;
    for (size_t i = 0; i < clip.notes.size(); ++i)
    {
        auto& n = clip.notes[i];
        if (n.start < beat1 && n.start + n.length > beat0 && n.pitch >= pitch0 && n.pitch <= pitch1)
            s.push_back ((int) i);
    }
    return s;
}

//==============================================================================
ClipPtr setLoop (const Clip& clip, double loopStart, double loopLength)
{
    auto copy = std::make_shared<Clip> (clip);
    loopStart = juce::jmax (0.0, loopStart);
    loopLength = juce::jmax (0.25, loopLength);

    const double end = loopStart + loopLength;
    if (end > copy->lengthBeats())
        copy->lengthBars = juce::jlimit (1, maxClipBars, (int) std::ceil (end / beatsPerBar - 1.0e-6));

    copy->loopStart = juce::jmin (loopStart, copy->lengthBeats() - 0.25);
    copy->loopLength = juce::jmin (loopLength, copy->lengthBeats() - copy->loopStart);
    return copy;
}

ClipPtr setClipBars (const Clip& clip, int bars)
{
    auto copy = std::make_shared<Clip> (clip);
    copy->lengthBars = juce::jlimit (1, maxClipBars, bars);
    if (copy->loopEnd() > copy->lengthBeats())
    {
        copy->loopStart = juce::jmin (copy->loopStart, copy->lengthBeats() - 0.25);
        copy->loopLength = copy->lengthBeats() - copy->loopStart;
    }
    copy->notes.erase (std::remove_if (copy->notes.begin(), copy->notes.end(),
                                       [&] (const Note& n) { return n.start >= copy->lengthBeats(); }),
                       copy->notes.end());
    return copy;
}

ClipPtr duplicateLoop (const Clip& clip)
{
    const double newEnd = clip.loopEnd() + clip.loopLength;
    if (newEnd > maxClipBars * (double) beatsPerBar + 1.0e-9)
        return std::make_shared<Clip> (clip);

    auto copy = std::make_shared<Clip> (clip);

    // Notes after the loop are pushed back to make room.
    std::vector<Note> notes;
    for (auto& n : clip.notes)
    {
        if (n.start >= clip.loopEnd())
        {
            auto moved = n;
            moved.start += clip.loopLength;
            notes.push_back (moved);
        }
        else
        {
            notes.push_back (n);
            if (n.start >= clip.loopStart)
            {
                auto copyNote = n;
                copyNote.start += clip.loopLength;
                notes.push_back (copyNote);
            }
        }
    }

    copy->notes = std::move (notes);
    copy->loopLength *= 2.0;
    copy->lengthBars = juce::jlimit (1, maxClipBars,
                                     juce::jmax (clip.lengthBars, (int) std::ceil ((clip.lengthBeats() + clip.loopLength) / beatsPerBar - 1.0e-6)));
    copy->sortNotes();
    return copy;
}

ClipPtr cropToLoop (const Clip& clip)
{
    auto copy = std::make_shared<Clip>();
    copy->lengthBars = juce::jlimit (1, maxClipBars, (int) std::ceil (clip.loopLength / beatsPerBar - 1.0e-6));
    copy->loopStart = 0.0;
    copy->loopLength = clip.loopLength;

    for (auto n : clip.notes)
        if (n.start >= clip.loopStart && n.start < clip.loopEnd())
        {
            n.start -= clip.loopStart;
            copy->notes.push_back (n);
        }

    for (auto lane : clip.automation)
    {
        AutomationLane cropped;
        cropped.target = lane.target;
        for (auto p : lane.points)
            if (p.beat >= clip.loopStart && p.beat <= clip.loopEnd())
                cropped.points.push_back ({ p.beat - clip.loopStart, p.value });
        if (! cropped.points.empty())
            copy->automation.push_back (std::move (cropped));
    }

    copy->sortNotes();
    return copy;
}

ClipPtr deleteLoopContent (const Clip& clip)
{
    auto copy = std::make_shared<Clip> (clip);
    copy->notes.erase (std::remove_if (copy->notes.begin(), copy->notes.end(), [&] (const Note& n)
                                       { return n.start >= clip.loopStart && n.start < clip.loopEnd(); }),
                       copy->notes.end());
    return copy;
}

ClipPtr mergeNotes (const Clip& clip, const std::vector<Note>& added)
{
    auto copy = std::make_shared<Clip> (clip);
    for (auto& n : added)
    {
        auto duplicate = std::find_if (copy->notes.begin(), copy->notes.end(), [&] (const Note& existing)
        {
            return existing.pitch == n.pitch && std::abs (existing.start - n.start) < 1.0 / 64.0;
        });

        if (duplicate != copy->notes.end())
            *duplicate = n;
        else
            copy->notes.push_back (n);
    }
    copy->sortNotes();
    return copy;
}

} // namespace rb
