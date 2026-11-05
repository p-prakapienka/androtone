#pragma once

#include <array>

// Tap tempo: every tap after the first sets the tempo from the average of the most recent
// intervals (up to MAX_INTERVALS), so more taps give a steadier result. A pause longer than
// RESET_SECONDS starts a new sequence. Times are in seconds from any monotonic clock.
class TapTempo {
public:
    static constexpr int MAX_INTERVALS = 4;
    static constexpr double RESET_SECONDS = 2.0;

    // Registers a tap. Returns the new tempo in BPM, or 0 when there isn't one yet (first tap).
    double tap(double timeSeconds) {
        if (hasLastTap && timeSeconds - lastTap > RESET_SECONDS) {
            count = 0;
            hasLastTap = false;
        }

        double bpm = 0.0;
        if (hasLastTap) {
            const double interval = timeSeconds - lastTap;
            if (interval > 0.0) {
                intervals[static_cast<size_t>(next)] = interval;
                next = (next + 1) % MAX_INTERVALS;
                if (count < MAX_INTERVALS) {
                    count++;
                }
                double sum = 0.0;
                for (int i = 0; i < count; i++) {
                    sum += intervals[static_cast<size_t>(i)];
                }
                bpm = 60.0 / (sum / count);
            }
        }

        lastTap = timeSeconds;
        hasLastTap = true;
        return bpm;
    }

private:
    std::array<double, MAX_INTERVALS> intervals {};
    int count = 0;
    int next = 0;
    double lastTap = 0.0;
    bool hasLastTap = false;
};
