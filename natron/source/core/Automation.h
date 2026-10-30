#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <vector>

// Parameter automation: a lane of (beat, value) points with linear interpolation.
//  - Pattern automation lives in a Pattern; beats are relative to the pattern start, and the target
//    is that pattern's machine (its parameters, mixer channel, or an effect).
//  - Song automation lives in the Song; beats are on the song timeline (song mode only), and the
//    target names a machine (or the master bus).
// Targets are by parameter id (resolved at play time), so they survive effect and machine changes.
struct AutomationTarget {
    enum class Scope { machine, mixer, effect, master };

    Scope scope = Scope::machine;
    int machine = 0;    // song automation only
    int effectSlot = 0; // Scope::effect only
    juce::String parameterId;

    bool operator==(const AutomationTarget& other) const {
        return scope == other.scope && machine == other.machine && effectSlot == other.effectSlot
               && parameterId == other.parameterId;
    }

    static juce::String scopeName(Scope scope) {
        switch (scope) {
            case Scope::mixer: return "mixer";
            case Scope::effect: return "effect";
            case Scope::master: return "master";
            case Scope::machine: break;
        }
        return "machine";
    }

    static bool scopeFromName(const juce::String& name, Scope& scope) {
        for (const auto s : { Scope::machine, Scope::mixer, Scope::effect, Scope::master }) {
            if (scopeName(s) == name) {
                scope = s;
                return true;
            }
        }
        return false;
    }
};

struct AutomationPoint {
    double beat = 0.0;
    float value = 0.0f;

    bool operator==(const AutomationPoint& other) const {
        return juce::exactlyEqual(beat, other.beat) && juce::exactlyEqual(value, other.value);
    }
};

struct AutomationLane {
    AutomationTarget target;
    std::vector<AutomationPoint> points; // sorted by beat

    // Linear between points; held flat before the first and after the last. No points: NaN-free 0.
    float valueAt(double beat) const {
        if (points.empty()) {
            return 0.0f;
        }
        if (beat <= points.front().beat) {
            return points.front().value;
        }
        if (beat >= points.back().beat) {
            return points.back().value;
        }
        const auto next = std::upper_bound(points.begin(), points.end(), beat,
                                           [](double b, const AutomationPoint& p) { return b < p.beat; });
        const auto previous = next - 1;
        const double span = next->beat - previous->beat;
        const double t = span > 0.0 ? (beat - previous->beat) / span : 0.0;
        return previous->value + static_cast<float>(t) * (next->value - previous->value);
    }

    // Adds a point (replacing one at the same beat), keeping the lane sorted.
    void setPoint(double beat, float value) {
        points.erase(std::remove_if(points.begin(), points.end(), [&](const AutomationPoint& p) { return std::abs(p.beat - beat) < 1.0e-9; }),
                     points.end());
        points.push_back({ beat, value });
        std::sort(points.begin(), points.end(), [](const AutomationPoint& a, const AutomationPoint& b) { return a.beat < b.beat; });
    }

    bool isSorted() const {
        return std::is_sorted(points.begin(), points.end(), [](const AutomationPoint& a, const AutomationPoint& b) { return a.beat < b.beat; });
    }

    bool operator==(const AutomationLane& other) const { return target == other.target && points == other.points; }
};

using AutomationLanes = std::vector<AutomationLane>;
