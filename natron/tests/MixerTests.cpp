// Channel strip and master bus behaviour.

#include "Analysis.h"
#include "TestHelpers.h"
#include "core/machines/DrumMachine.h"
#include "core/machines/SubtractiveSynth.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

namespace {
    constexpr double sampleRate = 48000.0;

    std::vector<Note> sustained() { return { { 0.0, 4.0, 57, 0.8f } }; }

    void pureSine(MachineRig& rig) {
        rig.set("osc1Shape", 3.0f);
        rig.set("oscMix", 0.0f);
        rig.set("cutoff", 20000.0f);
        rig.set("filterEnvAmount", 0.0f);
    }
}

TEST_CASE("Pan sends a channel hard left or right", "[mixer]") {
    MachineRig left(SubtractiveSynth::typeName);
    left.setMixer("pan", -1.0f);
    const auto l = left.play(sustained(), 0.3);
    CHECK(Analysis::rms(l, 0, 4800, 4800) > 0.01);
    CHECK(Analysis::rms(l, 1, 4800, 4800) < 1.0e-6);

    MachineRig right(SubtractiveSynth::typeName);
    right.setMixer("pan", 1.0f);
    const auto r = right.play(sustained(), 0.3);
    CHECK(Analysis::rms(r, 0, 4800, 4800) < 1.0e-6);
    CHECK(Analysis::rms(r, 1, 4800, 4800) > 0.01);
}

TEST_CASE("Width 0 makes a channel mono", "[mixer]") {
    MachineRig rig(DrumMachine::typeName);
    rig.set("s2.pan", -1.0f); // a hard-left snare...
    rig.setMixer("width", 0.0f); // ...folded to the centre
    const auto audio = rig.play({ { 0.0, 0.25, 37, 1.0f } }, 0.2);

    float maxDifference = 0.0f;
    for (int i = 0; i < audio.getNumSamples(); i++) {
        maxDifference = std::max(maxDifference, std::abs(audio.getSample(0, i) - audio.getSample(1, i)));
    }
    CHECK(maxDifference < 1.0e-6f);
    CHECK(Analysis::rms(audio, 1, 0, 4800) > 0.005);
}

TEST_CASE("Mute silences a channel, solo silences the others", "[mixer]") {
    Rig rig;
    const int synth = rig.session.addMachine(SubtractiveSynth::typeName);
    const int drums = rig.session.addMachine(DrumMachine::typeName);
    Pattern held;
    held.notes = sustained();
    rig.session.setPattern(synth, 0, held);
    Pattern kick;
    kick.notes = { { 0.0, 0.25, 36, 1.0f } };
    rig.session.setPattern(drums, 0, kick);

    rig.session.setMixerParameter(synth, "mute", 1.0f);
    rig.session.setMixerParameter(drums, "mute", 1.0f);
    rig.session.setPlaying(true);
    CHECK(analyse(rig.render(9600)).peak <= 0.0f);

    rig.session.setPlaying(false);
    rig.render(48000);
    rig.session.setMixerParameter(synth, "mute", 0.0f);
    rig.session.setMixerParameter(drums, "mute", 0.0f);
    rig.session.setMixerParameter(synth, "solo", 1.0f);
    rig.render(512); // the solo fades in over one block
    rig.session.setPlaying(true);
    const auto soloed = rig.render(24000);

    // Only the synth: no kick transient, and the steady synth level.
    MachineRig alone(SubtractiveSynth::typeName);
    const auto reference = alone.play(sustained(), 0.5);
    CHECK_THAT(Analysis::rms(soloed, 0, 4800, 9600), WithinAbs(Analysis::rms(reference, 0, 4800, 9600), 1.0e-4));
}

TEST_CASE("EQ bands cut and boost their range", "[mixer]") {
    auto bands = [](float low, float high) {
        MachineRig rig(SubtractiveSynth::typeName);
        rig.set("osc1Shape", 0.0f); // a bright saw covers the whole spectrum
        rig.set("oscMix", 0.0f);
        rig.set("cutoff", 20000.0f);
        rig.set("filterEnvAmount", 0.0f);
        rig.setMixer("eqLow", low);
        rig.setMixer("eqHigh", high);
        const auto audio = rig.play({ { 0.0, 4.0, 45, 0.8f } }, 0.5);
        return std::pair { Analysis::bandEnergy(audio, 8000, sampleRate, 50.0, 150.0),
                           Analysis::bandEnergy(audio, 8000, sampleRate, 8000.0, 16000.0) };
    };
    const auto flat = bands(0.0f, 0.0f);
    const auto lowCut = bands(-15.0f, 0.0f);
    const auto highBoost = bands(0.0f, 12.0f);

    CHECK(lowCut.first < 0.1 * flat.first);
    CHECK(highBoost.second > 5.0 * flat.second);
    CHECK_THAT(lowCut.second / flat.second, WithinAbs(1.0, 0.2)); // the other band barely moves
}

TEST_CASE("The delay echoes in time with the tempo", "[mixer][master]") {
    auto render = [](float send) {
        MachineRig rig(DrumMachine::typeName);
        rig.session.setTempo(120.0);
        rig.session.setMasterParameter("delayBeats", 0.5f); // 12000 samples at 120 BPM
        rig.session.setMasterParameter("delayFeedback", 0.0f);
        rig.set("s1.decay", 0.1f); // a short kick, so the echo stands alone
        rig.setMixer("delaySend", send);
        return rig.play({ { 0.0, 0.25, 36, 1.0f } }, 0.5, 1);
    };
    const auto dry = render(0.0f);
    const auto wet = render(1.0f);

    juce::AudioBuffer<float> echo(2, wet.getNumSamples());
    for (int ch = 0; ch < 2; ch++) {
        echo.copyFrom(ch, 0, wet, ch, 0, wet.getNumSamples());
        echo.addFrom(ch, 0, dry, ch, 0, dry.getNumSamples(), -1.0f);
    }
    const int start = Analysis::onset(echo, 0.01f);
    CAPTURE(start);
    CHECK(start >= 12000);
    CHECK(start < 12000 + 200);
}

TEST_CASE("The reverb send adds a tail", "[mixer][master]") {
    auto tail = [](float send) {
        MachineRig rig(DrumMachine::typeName);
        rig.setMixer("reverbSend", send);
        const auto audio = rig.play({ { 0.0, 0.25, 37, 1.0f } }, 1.0);
        return Analysis::rms(audio, 0, 24000, 12000); // 0.5-0.75 s, after the dry snare is gone
    };
    CHECK(tail(0.0f) < 1.0e-4);
    CHECK(tail(1.0f) > 2.0e-3);
}

TEST_CASE("The limiter holds the ceiling", "[mixer][master]") {
    const float ceilingDb = GENERATE(-0.3f, -6.0f);
    CAPTURE(ceilingDb);
    Rig rig;
    rig.session.setMasterParameter("volume", 1.5f);
    rig.session.setMasterParameter("limiterCeiling", ceilingDb);
    for (int i = 0; i < 6; i++) {
        const int m = rig.session.addMachine(DrumMachine::typeName);
        rig.session.setMachineVolume(m, 1.5f);
        Pattern loud;
        for (int step = 0; step < 16; step++) {
            loud.notes.push_back({ step * 0.25, 0.25, 36 + step % 2, 1.0f });
        }
        rig.session.setPattern(m, 0, loud);
    }
    rig.session.setPlaying(true);
    const auto stats = analyse(rig.render(96000));

    CHECK(stats.peak <= juce::Decibels::decibelsToGain(ceilingDb) + 1.0e-6f);
    CHECK(stats.peak > 0.5f * juce::Decibels::decibelsToGain(ceilingDb)); // limited, not silenced
}

TEST_CASE("Master volume scales everything", "[mixer][master]") {
    MachineRig rig(SubtractiveSynth::typeName);
    pureSine(rig);
    rig.session.setMasterParameter("volume", 0.0f);
    rig.render(512);
    CHECK(analyse(rig.play(sustained(), 0.3)).peak <= 0.0f);
}
