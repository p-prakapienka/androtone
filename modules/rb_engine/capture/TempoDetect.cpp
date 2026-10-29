namespace rb
{

TempoEstimate estimateTempo (const std::vector<double>& onsetsIn)
{
    TempoEstimate result;
    if (onsetsIn.size() < 2)
        return result;

    auto onsets = onsetsIn;
    std::sort (onsets.begin(), onsets.end());
    const double first = onsets.front();
    for (auto& t : onsets)
        t -= first;

    // Median inter-onset interval, ignoring near-simultaneous hits (chords, flams).
    std::vector<double> intervals;
    for (size_t i = 1; i < onsets.size(); ++i)
        if (onsets[i] - onsets[i - 1] > 0.05)
            intervals.push_back (onsets[i] - onsets[i - 1]);

    if (intervals.empty())
        return result;

    std::sort (intervals.begin(), intervals.end());
    const double medianInterval = intervals[intervals.size() / 2];

    // One loop pass: up to the last onset, plus the gap that would lead back to the start.
    const double period = onsets.back() + medianInterval;

    double bestScore = std::numeric_limits<double>::max();

    for (int bars : { 1, 2, 4, 8 })
    {
        const double centre = bars * beatsPerBar * 60.0 / period;
        if (centre < 60.0 || centre > 200.0)
            continue;

        for (double factor = 0.96; factor <= 1.04 + 1.0e-9; factor += 0.0025)
        {
            const double bpm = centre * factor;
            const double beatsPerSecond = bpm / 60.0;

            // How well onsets land on a 1/16 grid at this tempo (0 = perfectly).
            double error = 0.0;
            for (auto t : onsets)
            {
                const double sixteenths = t * beatsPerSecond * 4.0;
                const double d = sixteenths - std::round (sixteenths);
                error += d * d;
            }
            error /= (double) onsets.size();

            // Mild preference for tempos near 110 and for the length implied by the period.
            const double prior = std::pow (std::log2 (bpm / 110.0), 2.0) * 0.08 + std::abs (factor - 1.0) * 0.5;
            const double score = error + prior;

            if (score < bestScore)
            {
                bestScore = score;
                result.bpm = bpm;
                result.bars = bars;
            }
        }
    }

    result.bpm = juce::jlimit (minTempo, maxTempo, std::round (result.bpm * 10.0) / 10.0);
    return result;
}

} // namespace rb
