// Metronome clicks and tap tempo.

#include "TestHelpers.h"
#include "core/TapTempo.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

namespace {
    constexpr int64_t beatSamples = 24000; // 120 BPM at 48 kHz

    float peak(const juce::AudioBuffer<float>& audio, int64_t start, int64_t length) {
        return audio.getMagnitude(0, static_cast<int>(start), static_cast<int>(length));
    }

    int zeroCrossings(const juce::AudioBuffer<float>& audio, int64_t start, int64_t length) {
        int crossings = 0;
        for (auto i = start + 1; i < start + length; i++) {
            crossings += (audio.getSample(0, static_cast<int>(i - 1)) < 0.0f) != (audio.getSample(0, static_cast<int>(i)) < 0.0f);
        }
        return crossings;
    }
}

TEST_CASE("The metronome clicks on every beat, higher on the downbeat", "[metronome]") {
    for (const int blockSize : { 512, 100, 4096 }) { // where beats fall between blocks mustn't matter
        Rig rig(48000.0, blockSize);
        rig.engine.setMetronome(true);
        rig.session.setPlaying(true);
        const auto audio = rig.render(beatSamples * 6);

        for (int beat = 0; beat < 6; beat++) {
            CAPTURE(blockSize, beat);
            CHECK(peak(audio, beat * beatSamples, 480) > 0.1f);              // a click in the first 10 ms
            CHECK(peak(audio, beat * beatSamples + 2400, beatSamples - 2400) < 1.0e-6f); // silence after it
        }
        // Bars start on beats 0 and 4: their clicks are pitched higher.
        CHECK(zeroCrossings(audio, 0, 1200) > zeroCrossings(audio, beatSamples, 1200) + 10);
        CHECK(zeroCrossings(audio, 4 * beatSamples, 1200) > zeroCrossings(audio, 5 * beatSamples, 1200) + 10);
    }
}

TEST_CASE("No clicks when the metronome is off or the transport is stopped", "[metronome]") {
    Rig rig;
    rig.session.setPlaying(true);
    CHECK(peak(rig.render(beatSamples * 2), 0, beatSamples * 2) < 1.0e-6f);

    Rig stopped;
    stopped.engine.setMetronome(true);
    CHECK(peak(stopped.render(beatSamples * 2), 0, beatSamples * 2) < 1.0e-6f);
}

TEST_CASE("Tap tempo averages the last taps and restarts after a pause", "[metronome]") {
    TapTempo tapper;
    CHECK_FALSE(tapper.tap(10.0).has_value());
    CHECK_THAT(*tapper.tap(10.5), WithinAbs(120.0, 1.0e-9));
    CHECK_THAT(*tapper.tap(11.0), WithinAbs(120.0, 1.0e-9));
    CHECK_THAT(*tapper.tap(11.6), WithinAbs(60.0 / (1.6 / 3.0), 1.0e-9)); // average of 0.5, 0.5, 0.6

    for (double t = 12.0; t < 14.0; t += 0.4) { // only the last five taps count
        tapper.tap(t);
    }
    CHECK_THAT(*tapper.tap(14.0), WithinAbs(150.0, 1.0e-6));

    CHECK_FALSE(tapper.tap(20.0).has_value()); // a long pause starts a new count
    CHECK_THAT(*tapper.tap(21.0), WithinAbs(60.0, 1.0e-9));
}

TEST_CASE("Every beat clicks exactly once at any tempo and block size", "[metronome]") {
    // At 123 BPM beat 1 is at sample 23414.6; blocks of 105 and 223 end at 23415, right after it.
    for (const int blockSize : { 64, 105, 223, 333, 512 }) {
        Rig rig(48000.0, blockSize);
        rig.session.setTempo(123.0); // beats fall between samples
        rig.engine.setMetronome(true);
        rig.session.setPlaying(true);
        const double samplesPerBeat = 48000.0 * 60.0 / 123.0;
        const auto audio = rig.render(static_cast<int64_t>(samplesPerBeat * 16) + 10);

        std::vector<int> onsets;
        int quiet = 1000000;
        for (int i = 0; i < audio.getNumSamples(); i++) {
            if (std::abs(audio.getSample(0, i)) > 1.0e-4f) {
                if (quiet > 2400) {
                    onsets.push_back(i);
                }
                quiet = 0;
            } else {
                quiet++;
            }
        }
        CAPTURE(blockSize);
        REQUIRE(onsets.size() == 17);
        for (size_t beat = 0; beat < onsets.size(); beat++) {
            // The click starts on the first sample at or after the beat, at sine phase 0, so it is
            // first heard 0-2 samples after the exact beat.
            const double late = onsets[beat] - static_cast<double>(beat) * samplesPerBeat;
            CHECK(late >= 0.0);
            CHECK(late <= 2.0);
        }
    }
}
