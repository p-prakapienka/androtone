#pragma once

#include <juce_core/juce_core.h>
#include <deque>
#include <optional>

// Tap tempo: the tempo from the average gap between the last few taps. A pause longer than
// `resetSeconds` starts over. Times are in seconds from any clock.
class TapTempo {
public:
    static constexpr int maxTaps = 5;          // average the last four gaps
    static constexpr double resetSeconds = 2.0; // slower than 30 BPM: a new count

    // Returns the tempo once there are two taps or more.
    std::optional<double> tap(double seconds) {
        if (!taps.empty() && (seconds - taps.back() > resetSeconds || seconds <= taps.back())) {
            taps.clear();
        }
        taps.push_back(seconds);
        while (static_cast<int>(taps.size()) > maxTaps) {
            taps.pop_front();
        }
        if (taps.size() < 2) {
            return std::nullopt;
        }
        const double gap = (taps.back() - taps.front()) / static_cast<double>(taps.size() - 1);
        return 60.0 / gap;
    }

private:
    std::deque<double> taps;
};
