// Phase 4 machines, batch 1: Supersaw, Pluck, Organ, FM. 48 kHz, 120 BPM (a beat = 24000 samples).

#include "Analysis.h"
#include "TestHelpers.h"
#include "core/machines/FmSynth.h"
#include "core/machines/Organ.h"
#include "core/machines/Pluck.h"
#include "core/machines/Supersaw.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {
    constexpr double sampleRate = 48000.0;

    double stereoDifference(const juce::AudioBuffer<float>& audio, int start, int length) {
        double sum = 0.0;
        for (int i = start; i < start + length; i++) {
            sum += std::abs(audio.getSample(0, i) - audio.getSample(1, i));
        }
        return sum / length;
    }
}

// ---------------------------------------------------------------------------- every new machine

TEST_CASE("Phase 4 machines play in tune and stop cleanly", "[machines][4b]") {
    const juce::String type = GENERATE(as<juce::String> {}, Supersaw::typeName, Pluck::typeName, Organ::typeName, FmSynth::typeName);
    const int note = GENERATE(48, 69);
    CAPTURE(type, note);

    MachineRig rig(type);
    if (type == Supersaw::typeName) {
        rig.set("saws", 1.0f); // a single saw: one clear pitch
    }
    if (type == Organ::typeName) {
        rig.set("drawbar1", 0.0f); // 16' sounds an octave below the note: leave just 8' and 5 1/3'...
        rig.set("drawbar2", 0.0f); // ...and drop 5 1/3' too, so the fundamental is the played note
    }
    if (type == FmSynth::typeName) {
        rig.set("algorithm", 5.0f); // additive: all three sines at ratio 1
        for (const auto* op : { "op1", "op2", "op3" }) {
            rig.set(juce::String(op) + "Ratio", 1.0f);
        }
    }
    const auto audio = rig.play({ { 0.0, 1.0, note, 0.9f } }, 1.0);
    const double expected = juce::MidiMessage::getMidiNoteInHertz(note);
    CHECK_THAT(Analysis::pitch(audio, 2400, sampleRate), WithinRel(expected, 0.015));

    const auto stats = analyse(audio);
    CHECK(stats.allFinite);
    CHECK(stats.peak < 1.0f);
    CHECK(Analysis::rms(audio, 0, audio.getNumSamples() - 4800, 4800) < 1.0e-3); // released and quiet
}

// ---------------------------------------------------------------------------- Supersaw

TEST_CASE("Supersaw spreads its saws across the stereo field", "[machines][4b]") {
    auto widthWith = [](float saws) {
        MachineRig rig(Supersaw::typeName);
        rig.set("saws", saws);
        return stereoDifference(rig.play({ { 0.0, 2.0, 57, 0.8f } }, 0.4), 4800, 9600);
    };
    CHECK(widthWith(1.0f) < 1.0e-6);
    CHECK(widthWith(7.0f) > 0.01);
}

TEST_CASE("Supersaw detune thickens the sound", "[machines][4b]") {
    // With detune, the saws beat against each other: the level wobbles over time.
    auto wobble = [](float detune) {
        MachineRig rig(Supersaw::typeName);
        rig.set("detune", detune);
        rig.set("sideMix", 1.0f);
        const auto audio = rig.play({ { 0.0, 4.0, 45, 0.8f } }, 1.2);
        double low = 1.0e9, high = 0.0;
        for (int start = 4800; start + 2400 <= 52800; start += 2400) {
            const double level = Analysis::rms(audio, 0, start, 2400);
            low = std::min(low, level);
            high = std::max(high, level);
        }
        return high / low;
    };
    CHECK(wobble(1.0f) > wobble(0.0f) * 1.05);
}

// ---------------------------------------------------------------------------- Pluck

TEST_CASE("Pluck's fundamental decays at the set rate", "[machines][4b]") {
    // Decay is the time to -60 dB. Measure the fundamental's amplitude 0.5 s apart.
    auto amplitudeRatio = [](float decay) {
        MachineRig rig(Pluck::typeName);
        rig.set("decay", decay);
        rig.set("damping", 0.0f);
        const auto audio = rig.play({ { 0.0, 4.0, 57, 0.9f } }, 1.2);
        const double early = Analysis::bandEnergy(audio, 2400, sampleRate, 200.0, 240.0);
        const double late = Analysis::bandEnergy(audio, 2400 + 24000, sampleRate, 200.0, 240.0);
        return std::sqrt(late / early);
    };
    const double expectedLong = std::pow(10.0, -3.0 * 0.5 / 4.0);  // 4 s decay: -7.5 dB in 0.5 s
    CHECK_THAT(amplitudeRatio(4.0f), WithinRel(expectedLong, 0.25));
    CHECK(amplitudeRatio(0.3f) < 0.01); // 0.3 s decay: well past -40 dB
}

TEST_CASE("Pluck brightness shapes the attack", "[machines][4b]") {
    auto centroidAt = [](float brightness) {
        MachineRig rig(Pluck::typeName);
        rig.set("brightness", brightness);
        rig.set("damping", 0.0f);
        return Analysis::centroid(rig.play({ { 0.0, 2.0, 45, 0.9f } }, 0.3), 0, sampleRate);
    };
    CHECK(centroidAt(1.0f) > 1.5 * centroidAt(0.1f));
}

// ---------------------------------------------------------------------------- Organ

TEST_CASE("Organ drawbars add their harmonics", "[machines][4b]") {
    auto harmonicEnergy = [](float fourFoot) {
        MachineRig rig(Organ::typeName);
        rig.set("drawbar1", 0.0f);
        rig.set("drawbar2", 0.0f);
        rig.set("drawbar3", 8.0f); // 8' only: the fundamental
        rig.set("drawbar4", fourFoot); // 4': the octave
        rig.set("click", 0.0f);
        rig.set("rotarySpeed", 0.0f);
        const auto audio = rig.play({ { 0.0, 2.0, 57, 0.9f } }, 0.5);
        return std::pair { Analysis::bandEnergy(audio, 8000, sampleRate, 200.0, 240.0),
                           Analysis::bandEnergy(audio, 8000, sampleRate, 420.0, 460.0) };
    };
    const auto pure = harmonicEnergy(0.0f);
    const auto withOctave = harmonicEnergy(8.0f);
    CHECK(pure.second < 1.0e-3 * pure.first);
    CHECK(withOctave.second > 0.3 * withOctave.first);
}

TEST_CASE("Organ percussion adds a decaying attack", "[machines][4b]") {
    auto attackRatio = [](float percussion) {
        MachineRig rig(Organ::typeName);
        rig.set("percussion", percussion);
        rig.set("percussionLevel", 1.0f);
        rig.set("rotarySpeed", 0.0f);
        const auto audio = rig.play({ { 0.0, 4.0, 57, 0.9f } }, 1.0);
        return Analysis::rms(audio, 0, 480, 2400) / Analysis::rms(audio, 0, 36000, 2400);
    };
    CHECK(attackRatio(1.0f) > 1.3 * attackRatio(0.0f));
}

TEST_CASE("Organ rotary moves the sound between left and right", "[machines][4b]") {
    auto width = [](float speed) {
        MachineRig rig(Organ::typeName);
        rig.set("rotarySpeed", speed);
        rig.set("rotaryDepth", 1.0f);
        return stereoDifference(rig.play({ { 0.0, 4.0, 57, 0.9f } }, 1.5), 24000, 24000);
    };
    CHECK(width(0.0f) < 1.0e-6);
    CHECK(width(2.0f) > 0.005);
}

// ---------------------------------------------------------------------------- FM

TEST_CASE("FM carrier ratio sets the pitch", "[machines][4b]") {
    MachineRig rig(FmSynth::typeName);
    rig.set("algorithm", 5.0f);
    rig.set("op1Ratio", 2.0f);
    for (const auto* op : { "op2Level", "op3Level" }) {
        rig.set(op, 0.0f);
    }
    const auto audio = rig.play({ { 0.0, 2.0, 57, 0.9f } }, 0.4);
    CHECK_THAT(Analysis::pitch(audio, 2400, sampleRate), WithinRel(440.0, 0.01));
}

TEST_CASE("FM modulation brightens the carrier", "[machines][4b]") {
    auto centroidAt = [](float modulatorLevel) {
        MachineRig rig(FmSynth::typeName);
        rig.set("algorithm", 0.0f); // 3 > 2 > 1
        rig.set("op2Level", modulatorLevel);
        rig.set("op3Level", 0.0f);
        rig.set("op2Ratio", 1.0f);
        rig.set("op1Sustain", 1.0f);
        rig.set("op2Sustain", 1.0f);
        return Analysis::centroid(rig.play({ { 0.0, 2.0, 45, 0.9f } }, 0.5), 8000, sampleRate);
    };
    const double pure = centroidAt(0.0f);
    const double modulated = centroidAt(0.8f);
    CAPTURE(pure, modulated);
    CHECK(pure < 150.0);
    CHECK(modulated > 2.0 * pure);
}

TEST_CASE("FM has three operators", "[machines][4b]") {
    FmSynth fm;
    CHECK(fm.getParameters().indexOf("op3Ratio") >= 0);
    CHECK(fm.getParameters().indexOf("op4Ratio") < 0);
    CHECK(fm.getParameters().size() == FmSynth::numParameters);
}

TEST_CASE("FM algorithms define which operators are heard", "[machines][4b]") {
    for (int a = 0; a < FmSynth::numAlgorithms; a++) {
        CAPTURE(a);
        CHECK((FmSynth::carriers[a] & 1) == 1); // operator 1 is always a carrier
        for (int op = 0; op < FmSynth::numOperators; op++) {
            // Only higher operators modulate lower ones (no cycles).
            CHECK((FmSynth::modulators[a][op] & ((1 << (op + 1)) - 1)) == 0);
        }
    }
}
