#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Beats map to samples at a fixed tempo", "[transport]") {
    Transport transport;
    transport.prepare(48000.0);
    transport.setTempo(120.0);

    CHECK(juce::exactlyEqual(transport.samplesPerBeat(), 24000.0));
    CHECK(transport.sampleAt(0.0) == 0);
    CHECK(transport.sampleAt(0.25) == 6000);
    CHECK(transport.sampleAt(4.0) == 96000);
    CHECK(juce::exactlyEqual(transport.beatAt(36000), 1.5));
}

TEST_CASE("A beat between samples maps to the next sample", "[transport]") {
    Transport transport;
    transport.prepare(44100.0);
    transport.setTempo(130.0); // 20353.846... samples per beat

    CHECK(transport.sampleAt(1.0) == 20354);
    CHECK(transport.beatAt(transport.sampleAt(1.0)) >= 1.0);
    CHECK(transport.beatAt(transport.sampleAt(1.0) - 1) < 1.0);
}

TEST_CASE("A tempo change keeps time already played and rescales what follows", "[transport]") {
    Transport transport;
    transport.prepare(48000.0);
    transport.setTempo(120.0);

    transport.advance(48000); // 2 beats at 120
    transport.setTempo(60.0);

    CHECK(juce::exactlyEqual(transport.beatAt(48000), 2.0));
    CHECK(transport.sampleAt(3.0) == 48000 + 48000); // one beat at 60 BPM is 48000 samples
}

TEST_CASE("Rewind returns to beat 0 and keeps the tempo", "[transport]") {
    Transport transport;
    transport.prepare(48000.0);
    transport.setTempo(90.0);
    transport.advance(12345);
    transport.rewind();

    CHECK(transport.getPosition() == 0);
    CHECK(juce::exactlyEqual(transport.getTempo(), 90.0));
    CHECK(transport.sampleAt(1.0) == 32000);
}
