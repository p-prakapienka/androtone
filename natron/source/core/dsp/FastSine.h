#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>

// Table sine for inner loops (organ drawbars, FM operators, sine oscillators): 4096 points with
// linear interpolation, accurate to ~3e-7 — far below audibility, several times cheaper than std::sin.
namespace FastSine {
    constexpr int size = 4096;

    inline const std::array<float, size + 1>& table() {
        static const auto values = [] {
            std::array<float, size + 1> t {};
            for (int i = 0; i <= size; i++) {
                t[static_cast<size_t>(i)] = static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * i / size));
            }
            return t;
        }();
        return values;
    }

    // sin(2 pi * cycles), for any cycles (wraps).
    inline float ofCycles(double cycles) {
        const double wrapped = cycles - std::floor(cycles);
        const double position = wrapped * size;
        const auto index = static_cast<int>(position);
        const auto fraction = static_cast<float>(position - index);
        const auto& t = table();
        return t[static_cast<size_t>(index)] + fraction * (t[static_cast<size_t>(index + 1)] - t[static_cast<size_t>(index)]);
    }
}
