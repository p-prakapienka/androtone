namespace rb
{

float AutomationLane::valueAt (double beat) const
{
    if (points.empty())
        return 0.0f;
    if (beat <= points.front().beat)
        return points.front().value;
    if (beat >= points.back().beat)
        return points.back().value;

    auto next = std::upper_bound (points.begin(), points.end(), beat,
                                  [] (double b, const AutomationPoint& p) { return b < p.beat; });
    auto prev = next - 1;
    const double span = next->beat - prev->beat;
    if (span <= 0.0)
        return next->value;
    const double t = (beat - prev->beat) / span;
    return (float) (prev->value + (next->value - prev->value) * t);
}

void Clip::sortNotes()
{
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b)
    {
        if (a.start != b.start) return a.start < b.start;
        return a.pitch < b.pitch;
    });
}

const AutomationLane* Clip::findLane (const ParamTarget& target) const
{
    for (auto& lane : automation)
        if (lane.target == target)
            return &lane;
    return nullptr;
}

bool Track::hasClips() const
{
    for (auto& c : slots)
        if (c != nullptr)
            return true;
    return false;
}

int Set::indexOfTrack (int trackId) const
{
    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].id == trackId)
            return (int) i;
    return -1;
}

const Track* Set::findTrack (int trackId) const
{
    const int index = indexOfTrack (trackId);
    return index >= 0 ? &tracks[(size_t) index] : nullptr;
}

Track* Set::findTrack (int trackId)
{
    const int index = indexOfTrack (trackId);
    return index >= 0 ? &tracks[(size_t) index] : nullptr;
}

bool Set::hasAnyClip() const
{
    for (auto& t : tracks)
        if (t.hasClips())
            return true;
    return false;
}

bool Set::hasMelodicTrack() const
{
    for (auto& t : tracks)
        if (t.isMelodic())
            return true;
    return false;
}

int Set::shortestClipBars() const
{
    int shortest = 0;
    for (auto& t : tracks)
        for (auto& c : t.slots)
            if (c != nullptr)
            {
                const int bars = juce::jmax (1, (int) std::ceil (c->loopLength / beatsPerBar - 1.0e-6));
                shortest = shortest == 0 ? bars : juce::jmin (shortest, bars);
            }
    return shortest;
}

double Set::sceneLengthBeats (int scene) const
{
    double longest = 0.0;
    for (auto& t : tracks)
        if (auto& c = t.slots[(size_t) scene])
            longest = juce::jmax (longest, c->loopLength);
    return longest;
}

float volumeToDecibels (float position)
{
    if (position <= 0.0f)
        return -100.0f;
    // 0.8 -> 0 dB, 1.0 -> +6 dB, falling off steeply towards 0.
    if (position >= 0.8f)
        return (position - 0.8f) / 0.2f * 6.0f;
    return 40.0f * std::log10 (position / 0.8f);
}

float volumeToGain (float position)
{
    if (position <= 0.0f)
        return 0.0f;
    return juce::Decibels::decibelsToGain (volumeToDecibels (position));
}

const std::vector<RepeatRate>& getRepeatRates()
{
    static const std::vector<RepeatRate> rates {
        { "Off", 0.0 },
        { "1/4", 1.0 },  { "1/4T", 2.0 / 3.0 },
        { "1/8", 0.5 },  { "1/8T", 1.0 / 3.0 },
        { "1/16", 0.25 }, { "1/16T", 1.0 / 6.0 },
        { "1/32", 0.125 }, { "1/32T", 1.0 / 12.0 },
    };
    return rates;
}

} // namespace rb
