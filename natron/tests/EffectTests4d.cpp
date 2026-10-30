// The Phase 4 effects and the master EQ.

#include "Analysis.h"
#include "TestHelpers.h"
#include "core/effects/MoreEffects.h"
#include "core/machines/SubtractiveSynth.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {
    constexpr double sampleRate = 48000.0;

    juce::AudioBuffer<float> sine(double frequency, float level = 0.5f, int length = 48000) {
        juce::AudioBuffer<float> buffer(2, length);
        for (int i = 0; i < length; i++) {
            const auto x = static_cast<float>(level * std::sin(juce::MathConstants<double>::twoPi * frequency * i / sampleRate));
            buffer.setSample(0, i, x);
            buffer.setSample(1, i, x);
        }
        return buffer;
    }

    juce::AudioBuffer<float> noise(float level = 0.5f, int length = 48000) {
        juce::AudioBuffer<float> buffer(2, length);
        juce::Random random(3);
        for (int i = 0; i < length; i++) {
            const float x = level * (random.nextFloat() * 2.0f - 1.0f);
            buffer.setSample(0, i, x);
            buffer.setSample(1, i, x);
        }
        return buffer;
    }

    juce::AudioBuffer<float> run(Effect& effect, juce::AudioBuffer<float> input) {
        effect.prepare(sampleRate, 512);
        juce::AudioBuffer<float> block(2, 512);
        for (int start = 0; start < input.getNumSamples(); start += 512) {
            const int n = juce::jmin(512, input.getNumSamples() - start);
            for (int ch = 0; ch < 2; ch++) {
                block.copyFrom(ch, 0, input, ch, start, n);
            }
            effect.process(block, n);
            for (int ch = 0; ch < 2; ch++) {
                input.copyFrom(ch, start, block, ch, 0, n);
            }
        }
        return input;
    }

    void set(Effect& effect, const juce::String& id, float value) {
        auto& parameters = effect.getParameters();
        REQUIRE(parameters.indexOf(id) >= 0);
        parameters.set(parameters.indexOf(id), value);
    }
}

TEST_CASE("AutoWah opens with the input level", "[effects][4d]") {
    // A 2 kHz tone: quiet, the filter stays near its 300 Hz base and cuts it; loud, the envelope
    // sweeps the filter up (~2.4 kHz at full range) and it passes.
    auto gainAt = [](float level) {
        AutoWah wah;
        set(wah, "sensitivity", 1.0f);
        const auto in = sine(2000.0, level);
        return Analysis::rms(run(wah, in), 0, 24000, 4800) / Analysis::rms(in, 0, 24000, 4800);
    };
    const double quiet = gainAt(0.01f);
    const double loud = gainAt(0.8f);
    CAPTURE(quiet, loud);
    CHECK(loud > 4.0 * quiet);
}

TEST_CASE("Limiter holds peaks at its threshold", "[effects][4d]") {
    Limiter limiter;
    set(limiter, "threshold", -12.0f);
    const auto out = run(limiter, sine(220.0, 0.95f));
    CHECK(analyse(out).peak <= juce::Decibels::decibelsToGain(-12.0f) + 1.0e-5f);
    CHECK(analyse(out).peak > 0.2f);

    Limiter withMakeup;
    set(withMakeup, "threshold", -12.0f);
    set(withMakeup, "makeup", 12.0f);
    CHECK_THAT(analyse(run(withMakeup, sine(220.0, 0.95f))).peak, WithinAbs(1.0, 0.02));
}

TEST_CASE("Vinyl adds noise and narrows the bandwidth", "[effects][4d]") {
    Vinyl vinyl;
    set(vinyl, "wow", 0.0f);
    juce::AudioBuffer<float> silence(2, 48000);
    silence.clear();
    CHECK(analyse(run(vinyl, silence)).rms > 1.0e-3); // hiss and crackle on silence

    Vinyl narrow;
    set(narrow, "hiss", 0.0f);
    set(narrow, "crackle", 0.0f);
    set(narrow, "tone", 1.0f);
    const auto in = noise();
    const auto out = run(narrow, in);
    CHECK(Analysis::bandEnergy(out, 24000, sampleRate, 8000.0, 20000.0)
          < 0.01 * Analysis::bandEnergy(in, 24000, sampleRate, 8000.0, 20000.0));
}

TEST_CASE("Comb filter resonates at its frequency and harmonics", "[effects][4d]") {
    Comb comb;
    set(comb, "frequency", 500.0f);
    set(comb, "feedback", 0.9f);
    set(comb, "damping", 0.0f);
    set(comb, "mix", 1.0f);
    const auto out = run(comb, noise(0.1f));
    const double peak = Analysis::bandEnergy(out, 24000, sampleRate, 990.0, 1010.0);   // 2nd harmonic
    const double between = Analysis::bandEnergy(out, 24000, sampleRate, 1240.0, 1260.0); // halfway to the 3rd
    CHECK(peak > 20.0 * between);
}

TEST_CASE("Cabinet rolls off lows and highs", "[effects][4d]") {
    Cabinet cabinet;
    const auto in = noise();
    const auto out = run(cabinet, in);
    CHECK(Analysis::bandEnergy(out, 24000, sampleRate, 10000.0, 20000.0) < 0.02 * Analysis::bandEnergy(in, 24000, sampleRate, 10000.0, 20000.0));
    CHECK(Analysis::bandEnergy(out, 24000, sampleRate, 20.0, 40.0) < 0.2 * Analysis::bandEnergy(in, 24000, sampleRate, 20.0, 40.0));
    CHECK(Analysis::bandEnergy(out, 24000, sampleRate, 800.0, 1200.0) > 0.3 * Analysis::bandEnergy(in, 24000, sampleRate, 800.0, 1200.0));
}

TEST_CASE("Octaver adds one and two octaves below", "[effects][4d]") {
    Octaver octaver;
    set(octaver, "dry", 0.0f);
    set(octaver, "tone", 1.0f);
    const auto out = run(octaver, sine(440.0));
    const double fundamental = Analysis::bandEnergy(out, 24000, sampleRate, 430.0, 450.0);
    CHECK(Analysis::bandEnergy(out, 24000, sampleRate, 215.0, 225.0) > 10.0 * fundamental);
    CHECK(Analysis::bandEnergy(out, 24000, sampleRate, 105.0, 115.0) > 10.0 * fundamental);
}

TEST_CASE("Vibrato wobbles the pitch", "[effects][4d]") {
    Vibrato vibrato;
    set(vibrato, "rate", 2.0f);
    set(vibrato, "depth", 1.0f);
    const auto out = run(vibrato, sine(440.0, 0.5f, 96000));
    double lowest = 1.0e9, highest = 0.0;
    for (int start = 4800; start + 2048 < 96000; start += 1200) {
        const double p = Analysis::pitch(out, start, sampleRate, 2048);
        lowest = std::min(lowest, p);
        highest = std::max(highest, p);
    }
    CHECK(Analysis::semitonesBetween(highest, lowest) > 0.3);
}

TEST_CASE("Tremolo and AutoPan modulate level and position", "[effects][4d]") {
    Tremolo tremolo;
    set(tremolo, "rate", 4.0f);
    set(tremolo, "depth", 1.0f);
    const auto trem = run(tremolo, sine(1000.0));
    double quietest = 1.0, loudest = 0.0;
    for (int start = 0; start + 600 <= 24000; start += 600) {
        const double level = Analysis::rms(trem, 0, start, 600);
        quietest = std::min(quietest, level);
        loudest = std::max(loudest, level);
    }
    CHECK(quietest < 0.1 * loudest);

    AutoPan pan;
    set(pan, "rate", 2.0f);
    set(pan, "depth", 1.0f);
    const auto panned = run(pan, sine(1000.0));
    // A quarter period in (sine LFO at its peak): hard right; three quarters: hard left.
    CHECK(Analysis::rms(panned, 0, 6000 - 300, 600) < 0.1 * Analysis::rms(panned, 1, 6000 - 300, 600));
    CHECK(Analysis::rms(panned, 1, 18000 - 300, 600) < 0.1 * Analysis::rms(panned, 0, 18000 - 300, 600));
}

TEST_CASE("Static Flanger and Comb are dry at mix 0", "[effects][4d]") {
    const juce::String type = GENERATE(as<juce::String> {}, "Comb", "Static Flanger");
    CAPTURE(type);
    auto effect = EffectRegistry::create(type);
    set(*effect, "mix", 0.0f);
    const auto in = noise();
    const auto out = run(*effect, in);
    float difference = 0.0f;
    for (int i = 0; i < in.getNumSamples(); i++) {
        difference = std::max(difference, std::abs(out.getSample(0, i) - in.getSample(0, i)));
    }
    CHECK(difference < 1.0e-6f);
}

TEST_CASE("Master EQ boosts its band", "[mixer][master][4d]") {
    auto levelAt = [](float gain) {
        MachineRig rig(SubtractiveSynth::typeName);
        rig.set("osc1Shape", 3.0f);
        rig.set("oscMix", 0.0f);
        rig.set("cutoff", 20000.0f);
        rig.set("filterEnvAmount", 0.0f);
        rig.session.setMasterParameter("eq2Freq", 440.0f);
        rig.session.setMasterParameter("eq2Gain", gain);
        rig.session.setMasterParameter("eq2Q", 2.0f);
        return Analysis::rms(rig.play({ { 0.0, 4.0, 69, 0.5f } }, 0.4), 0, 9600, 4800);
    };
    CHECK_THAT(juce::Decibels::gainToDecibels(levelAt(6.0f) / levelAt(0.0f)), WithinAbs(6.0, 0.5));
}
