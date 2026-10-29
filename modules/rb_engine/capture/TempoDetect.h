#pragma once

namespace rb
{

struct TempoEstimate
{
    double bpm = 120.0;
    int bars = 1;
};

// Estimates tempo and loop length (1, 2, 4 or 8 bars of 4/4) from note onsets played
// freely with the transport stopped (CAP-3). The first onset is taken as the downbeat and
// the performance is assumed to be one pass of a loop.
TempoEstimate estimateTempo (const std::vector<double>& onsetSeconds);

} // namespace rb
