#pragma once

// Tolerance-based golden renders. Instead of storing audio, a render is reduced to a fingerprint —
// RMS and spectral centroid per 50 ms window — saved under tests/golden/. Tests compare a fresh
// render against it, catching unintended changes to how machines and the mixer sound without being
// brittle to last-bit floating-point differences between compilers.
//
// After an intended sound change, regenerate: NATRON_UPDATE_GOLDEN=1 ./NatronTests "[golden]"
// and review the diff of tests/golden/ like any other change.

#include "Analysis.h"

#include <juce_core/juce_core.h>

#ifndef NATRON_GOLDEN_DIR
    #error "NATRON_GOLDEN_DIR must point at tests/golden (set by CMakeLists.txt)"
#endif

namespace Fingerprint {
    struct Frame {
        double rms;
        double centroid;
    };

    inline std::vector<Frame> compute(const juce::AudioBuffer<float>& audio, double sampleRate) {
        const int window = static_cast<int>(sampleRate * 0.05);
        std::vector<Frame> frames;
        for (int start = 0; start + window <= audio.getNumSamples(); start += window) {
            const double level = 0.5 * (Analysis::rms(audio, 0, start, window) + Analysis::rms(audio, 1, start, window));
            frames.push_back({ level, Analysis::centroid(audio, start, sampleRate) });
        }
        return frames;
    }

    inline juce::File fileFor(const juce::String& name) {
        return juce::File(NATRON_GOLDEN_DIR).getChildFile(name + ".txt");
    }

    inline bool shouldUpdate() {
        return juce::SystemStats::getEnvironmentVariable("NATRON_UPDATE_GOLDEN", {}).isNotEmpty();
    }

    inline void save(const juce::String& name, const std::vector<Frame>& frames) {
        juce::String text = "# rms centroidHz, one line per 50 ms window\n";
        for (const auto& f : frames) {
            text << juce::String(f.rms, 6) << " " << juce::String(f.centroid, 1) << "\n";
        }
        fileFor(name).replaceWithText(text);
    }

    inline std::vector<Frame> load(const juce::String& name) {
        std::vector<Frame> frames;
        juce::StringArray lines;
        lines.addLines(fileFor(name).loadFileAsString());
        for (const auto& line : lines) {
            if (line.startsWith("#") || line.trim().isEmpty()) {
                continue;
            }
            const auto parts = juce::StringArray::fromTokens(line, " ", "");
            frames.push_back({ parts[0].getDoubleValue(), parts[1].getDoubleValue() });
        }
        return frames;
    }

    // Returns an empty string if `actual` matches the stored fingerprint, else a description.
    inline juce::String compare(const juce::String& name, const std::vector<Frame>& actual) {
        if (shouldUpdate()) {
            save(name, actual);
            return {};
        }
        if (!fileFor(name).existsAsFile()) {
            return "missing " + fileFor(name).getFullPathName() + " (run with NATRON_UPDATE_GOLDEN=1)";
        }
        const auto expected = load(name);
        if (expected.size() != actual.size()) {
            return "frame count " + juce::String(static_cast<int>(actual.size())) + " != "
                   + juce::String(static_cast<int>(expected.size()));
        }
        for (size_t i = 0; i < expected.size(); i++) {
            const auto& e = expected[i];
            const auto& a = actual[i];
            const juce::String where = "frame " + juce::String(static_cast<int>(i)) + " (" + juce::String(static_cast<int>(i) * 50) + " ms): ";
            if (std::abs(a.rms - e.rms) > 0.05 * e.rms + 2.0e-4) {
                return where + "rms " + juce::String(a.rms, 6) + " vs " + juce::String(e.rms, 6);
            }
            if (e.rms > 1.0e-3 && std::abs(a.centroid - e.centroid) > 0.05 * e.centroid + 20.0) {
                return where + "centroid " + juce::String(a.centroid, 1) + " vs " + juce::String(e.centroid, 1);
            }
        }
        return {};
    }
}
