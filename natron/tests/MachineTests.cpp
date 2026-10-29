// Behaviour of the Phase 2 machines, measured from offline renders.
// 48 kHz, 120 BPM: a beat is 24000 samples.

#include "Analysis.h"
#include "TestHelpers.h"
#include "core/machines/AcidBass.h"
#include "core/machines/DrumMachine.h"
#include "core/machines/SubtractiveSynth.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {
    constexpr double sampleRate = 48000.0;
    constexpr int beat = 24000;

    // A Subtractive patch reduced to a pure sine through an open filter.
    void pureSine(MachineRig& rig) {
        rig.set("osc1Shape", 3.0f);
        rig.set("osc2Shape", 3.0f);
        rig.set("oscMix", 0.0f);
        rig.set("cutoff", 20000.0f);
        rig.set("filterEnvAmount", 0.0f);
        rig.set("resonance", 0.0f);
    }
}

// ---------------------------------------------------------------------------- Subtractive

TEST_CASE("Subtractive plays the note's pitch", "[machines][subtractive]") {
    const int note = GENERATE(45, 57, 69, 81);
    CAPTURE(note);
    MachineRig rig(SubtractiveSynth::typeName);
    pureSine(rig);

    const auto audio = rig.play({ { 0.0, 2.0, note, 0.8f } }, 0.5);
    const double expected = juce::MidiMessage::getMidiNoteInHertz(note);
    CHECK_THAT(Analysis::pitch(audio, 4000, sampleRate), WithinRel(expected, 0.01));
}

TEST_CASE("Subtractive osc 2 is offset in semitones", "[machines][subtractive]") {
    MachineRig rig(SubtractiveSynth::typeName);
    pureSine(rig);
    rig.set("oscMix", 1.0f);
    rig.set("osc2Semitones", 7.0f);
    rig.set("osc2Fine", 0.0f);

    const auto audio = rig.play({ { 0.0, 2.0, 57, 0.8f } }, 0.5);
    CHECK_THAT(Analysis::semitonesBetween(Analysis::pitch(audio, 4000, sampleRate), 220.0), WithinAbs(7.0, 0.1));
}

TEST_CASE("Subtractive cutoff darkens the sound", "[machines][subtractive]") {
    auto centroidAt = [](float cutoff) {
        MachineRig rig(SubtractiveSynth::typeName);
        rig.set("osc1Shape", 0.0f); // saw
        rig.set("oscMix", 0.0f);
        rig.set("filterEnvAmount", 0.0f);
        rig.set("cutoff", cutoff);
        return Analysis::centroid(rig.play({ { 0.0, 2.0, 45, 0.8f } }, 0.5), 8000, sampleRate);
    };
    const double dark = centroidAt(300.0f);
    const double bright = centroidAt(8000.0f);
    CAPTURE(dark, bright);
    CHECK(bright > 3.0 * dark);
}

TEST_CASE("Subtractive high-pass removes the fundamental", "[machines][subtractive]") {
    auto lowEnergy = [](float type) {
        MachineRig rig(SubtractiveSynth::typeName);
        rig.set("osc1Shape", 0.0f);
        rig.set("oscMix", 0.0f);
        rig.set("filterEnvAmount", 0.0f);
        rig.set("cutoff", 1000.0f);
        rig.set("filterType", type);
        return Analysis::bandEnergy(rig.play({ { 0.0, 2.0, 45, 0.8f } }, 0.5), 8000, sampleRate, 50.0, 300.0);
    };
    CHECK(lowEnergy(1.0f) < 0.05 * lowEnergy(0.0f));
}

TEST_CASE("Subtractive amp release sets the tail length", "[machines][subtractive]") {
    auto tailLevel = [](float release) {
        MachineRig rig(SubtractiveSynth::typeName);
        pureSine(rig);
        rig.set("ampRelease", release);
        const auto audio = rig.play({ { 0.0, 1.0, 69, 0.8f } }, 1.0); // note ends at beat 1 (24000)
        return Analysis::rms(audio, 0, beat + 9600, 2400); // 200-250 ms after the note-off
    };
    CHECK(tailLevel(0.02f) < 1.0e-4);
    CHECK(tailLevel(1.0f) > 0.01);
}

TEST_CASE("Subtractive is polyphonic", "[machines][subtractive]") {
    MachineRig rig(SubtractiveSynth::typeName);
    pureSine(rig);
    const auto one = rig.play({ { 0.0, 2.0, 60, 0.8f } }, 0.4);

    MachineRig chordRig(SubtractiveSynth::typeName);
    pureSine(chordRig);
    std::vector<Note> chord;
    for (const int pitch : { 48, 55, 60, 64, 67, 71, 74, 77, 81 }) { // 9 notes: one voice is stolen
        chord.push_back({ 0.0, 2.0, pitch, 0.8f });
    }
    const auto many = chordRig.play(chord, 0.4);

    CHECK(Analysis::rms(many, 0, 4800, 9600) > 2.0 * Analysis::rms(one, 0, 4800, 9600));
    CHECK(analyse(many).allFinite);
}

TEST_CASE("Subtractive LFO moves the pitch", "[machines][subtractive]") {
    MachineRig rig(SubtractiveSynth::typeName);
    pureSine(rig);
    rig.set("lfoRate", 1.0f);
    rig.set("lfoToPitch", 1.0f); // +-1 semitone
    const auto audio = rig.play({ { 0.0, 4.0, 69, 0.8f } }, 1.2);

    // Quarter of the way through the LFO cycle the pitch is up, three quarters down.
    const double up = Analysis::pitch(audio, 12000 - 1024, sampleRate, 2048);
    const double down = Analysis::pitch(audio, 36000 - 1024, sampleRate, 2048);
    CHECK(Analysis::semitonesBetween(up, 440.0) > 0.5);
    CHECK(Analysis::semitonesBetween(down, 440.0) < -0.5);
}

// ---------------------------------------------------------------------------- Acid

TEST_CASE("Acid is monophonic: the last note wins", "[machines][acid]") {
    MachineRig rig(AcidBass::typeName);
    rig.set("cutoff", 4000.0f);
    rig.set("envMod", 0.0f);
    rig.set("slide", 0.005f);
    const auto audio = rig.play({ { 0.0, 1.0, 45, 0.8f }, { 0.0, 1.0, 52, 0.8f } }, 0.4);

    CHECK_THAT(Analysis::semitonesBetween(Analysis::pitch(audio, 6000, sampleRate), 110.0), WithinAbs(7.0, 0.2));
}

TEST_CASE("Acid slides between overlapping notes", "[machines][acid]") {
    MachineRig rig(AcidBass::typeName);
    rig.set("cutoff", 4000.0f);
    rig.set("envMod", 0.0f);
    rig.set("slide", 0.08f);
    // 45 is held past the start of 57: slide.
    const auto audio = rig.play({ { 0.0, 0.6, 45, 0.8f }, { 0.5, 1.5, 57, 0.8f } }, 0.9);

    const double justAfter = Analysis::semitonesBetween(Analysis::pitch(audio, 12000 + 1200, sampleRate, 1536), 110.0);
    // Five glide time constants later (0.4 s) it has arrived.
    const double later = Analysis::semitonesBetween(Analysis::pitch(audio, 12000 + 19200, sampleRate, 2048), 110.0);
    CAPTURE(justAfter, later);
    CHECK(justAfter > 1.0);
    CHECK(justAfter < 11.0); // still gliding
    CHECK_THAT(later, WithinAbs(12.0, 0.3));
}

TEST_CASE("Acid jumps between separate notes", "[machines][acid]") {
    MachineRig rig(AcidBass::typeName);
    rig.set("cutoff", 4000.0f);
    rig.set("envMod", 0.0f);
    rig.set("slide", 0.08f);
    const auto audio = rig.play({ { 0.0, 0.4, 45, 0.8f }, { 0.5, 0.4, 57, 0.8f } }, 0.6);

    const double justAfter = Analysis::semitonesBetween(Analysis::pitch(audio, 12000 + 600, sampleRate, 1536), 110.0);
    CHECK_THAT(justAfter, WithinAbs(12.0, 0.3));
}

TEST_CASE("Acid accents are louder", "[machines][acid]") {
    auto peakFor = [](float velocity) {
        MachineRig rig(AcidBass::typeName);
        rig.setMixer("volume", 0.5f); // keep clear of the master limiter
        return analyse(rig.play({ { 0.0, 0.25, 45, velocity } }, 0.3)).peak;
    };
    const float normal = peakFor(0.8f);
    const float accented = peakFor(1.0f);
    CAPTURE(normal, accented);
    CHECK(accented > 1.3f * normal);
}

TEST_CASE("Acid gate closes quickly after the note", "[machines][acid]") {
    MachineRig rig(AcidBass::typeName);
    const auto audio = rig.play({ { 0.0, 0.25, 45, 0.8f } }, 0.5);
    CHECK(Analysis::rms(audio, 0, 6000 + 1500, 4800) < 1.0e-3); // 30 ms after the note-off
    CHECK(Analysis::rms(audio, 0, 1000, 4000) > 1.0e-2);
}

// ---------------------------------------------------------------------------- Drums

TEST_CASE("Every drum slot sounds, on its own note", "[machines][drums]") {
    const int slot = GENERATE(range(0, DrumMachine::numSlots));
    CAPTURE(slot, DrumKit::names()[static_cast<size_t>(slot)]);
    MachineRig rig(DrumMachine::typeName);
    const auto audio = rig.play({ { 0.0, 0.25, DrumMachine::firstNote + slot, 1.0f } }, 0.3);
    CHECK(Analysis::rms(audio, 0, 0, 4800) > 0.01);
}

TEST_CASE("Notes outside the kit are silent", "[machines][drums]") {
    MachineRig rig(DrumMachine::typeName);
    const auto audio = rig.play({ { 0.0, 0.25, 35, 1.0f }, { 0.0, 0.25, 44, 1.0f } }, 0.3);
    CHECK(analyse(audio).peak <= 0.0f);
}

TEST_CASE("Kick is low, hats are high", "[machines][drums]") {
    auto centroidOf = [](int note) {
        MachineRig rig(DrumMachine::typeName);
        return Analysis::centroid(rig.play({ { 0.0, 0.25, note, 1.0f } }, 0.2), 0, sampleRate);
    };
    const double kick = centroidOf(36);
    const double hat = centroidOf(38);
    CAPTURE(kick, hat);
    CHECK(kick < 500.0);
    CHECK(hat > 5000.0);
}

TEST_CASE("Drum mute, pan, and tune work per slot", "[machines][drums]") {
    SECTION("mute") {
        MachineRig rig(DrumMachine::typeName);
        rig.set("s1.mute", 1.0f);
        CHECK(analyse(rig.play({ { 0.0, 0.25, 36, 1.0f } }, 0.3)).peak <= 0.0f);
    }
    SECTION("pan") {
        MachineRig rig(DrumMachine::typeName);
        rig.set("s2.pan", -1.0f);
        const auto audio = rig.play({ { 0.0, 0.25, 37, 1.0f } }, 0.3);
        CHECK(Analysis::rms(audio, 0, 0, 4800) > 0.01);
        CHECK(Analysis::rms(audio, 1, 0, 4800) <= 0.0);
    }
    SECTION("tune") {
        auto lengthAt = [](float semitones) {
            MachineRig rig(DrumMachine::typeName);
            rig.set("s6.tune", semitones);
            const auto audio = rig.play({ { 0.0, 0.25, 41, 1.0f } }, 1.0);
            int last = 0;
            for (int i = 0; i < audio.getNumSamples(); i++) {
                if (std::abs(audio.getSample(0, i)) > 1.0e-3f) {
                    last = i;
                }
            }
            return last;
        };
        const int normal = lengthAt(0.0f);
        const int octaveUp = lengthAt(12.0f);
        CAPTURE(normal, octaveUp);
        CHECK_THAT(static_cast<double>(octaveUp) / normal, WithinAbs(0.5, 0.08));
    }
    SECTION("decay") {
        MachineRig rig(DrumMachine::typeName);
        rig.set("s4.decay", 0.1f);
        const auto shortened = rig.play({ { 0.0, 0.25, 39, 1.0f } }, 0.5);
        MachineRig full(DrumMachine::typeName);
        const auto whole = full.play({ { 0.0, 0.25, 39, 1.0f } }, 0.5);
        CHECK(Analysis::rms(shortened, 0, 9600, 4800) < 0.1 * Analysis::rms(whole, 0, 9600, 4800));
    }
}

TEST_CASE("The drum kit is deterministic", "[machines][drums]") {
    const auto a = DrumKit::generate();
    const auto b = DrumKit::generate();
    for (size_t s = 0; s < a.size(); s++) {
        REQUIRE(a[s].getNumSamples() == b[s].getNumSamples());
        for (int i = 0; i < a[s].getNumSamples(); i++) {
            if (!juce::exactlyEqual(a[s].getSample(0, i), b[s].getSample(0, i))) {
                FAIL("sound " << s << " differs at sample " << i);
            }
        }
    }
}
