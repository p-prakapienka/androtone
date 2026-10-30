// Insert effects: standalone behaviour, and as machine inserts through a Session.

#include "Analysis.h"
#include "TestHelpers.h"
#include "core/effects/Effects.h"
#include "core/machines/SubtractiveSynth.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <set>

using Catch::Matchers::WithinAbs;

namespace {
    constexpr double sampleRate = 48000.0;
    constexpr int length = 48000;

    juce::AudioBuffer<float> sine(double frequency, float level = 0.5f) {
        juce::AudioBuffer<float> buffer(2, length);
        for (int i = 0; i < length; i++) {
            const auto x = static_cast<float>(level * std::sin(juce::MathConstants<double>::twoPi * frequency * i / sampleRate));
            buffer.setSample(0, i, x);
            buffer.setSample(1, i, x);
        }
        return buffer;
    }

    // Runs `effect` over `input` in 512-sample blocks.
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
        parameters.set(parameters.indexOf(id), value);
    }

    float maxDifference(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from = 0) {
        float d = 0.0f;
        for (int ch = 0; ch < 2; ch++) {
            for (int i = from; i < a.getNumSamples(); i++) {
                d = std::max(d, std::abs(a.getSample(ch, i) - b.getSample(ch, i)));
            }
        }
        return d;
    }
}

TEST_CASE("Every effect type is registered and produces finite output", "[effects]") {
    const auto types = EffectRegistry::getTypeNames();
    CHECK(types.size() == 18);
    for (const auto& type : types) {
        CAPTURE(type);
        auto effect = EffectRegistry::create(type);
        REQUIRE(effect != nullptr);
        CHECK(effect->getTypeName() == type);
        const auto out = run(*effect, sine(220.0, 0.9f));
        CHECK(analyse(out).allFinite);
        CHECK(analyse(out).rms > 1.0e-3);
    }
    CHECK(EffectRegistry::create("NoSuchEffect") == nullptr);
}

TEST_CASE("Distortion adds harmonics", "[effects]") {
    Distortion distortion;
    set(distortion, "tone", 20000.0f);
    const auto clean = sine(220.0);
    const auto driven = run(distortion, clean);
    CHECK(Analysis::bandEnergy(driven, 8192, sampleRate, 600.0, 700.0) // 3rd harmonic
          > 100.0 * Analysis::bandEnergy(clean, 8192, sampleRate, 600.0, 700.0));
    CHECK(analyse(driven).peak <= juce::Decibels::decibelsToGain(-6.0f) + 1.0e-4f); // tanh * output gain
}

TEST_CASE("Bitcrusher quantises to its bit depth", "[effects]") {
    Bitcrusher crusher;
    set(crusher, "bits", 3.0f);
    set(crusher, "downsample", 1.0f);
    const auto out = run(crusher, sine(220.0, 0.9f));
    std::set<float> values;
    for (int i = 0; i < out.getNumSamples(); i++) {
        values.insert(out.getSample(0, i));
    }
    CHECK(values.size() <= 9); // 2^(3-1) levels each side of zero, plus zero
}

TEST_CASE("Filter effect removes highs in low-pass mode", "[effects]") {
    FilterEffect filter;
    set(filter, "cutoff", 300.0f);
    juce::AudioBuffer<float> input(2, length);
    juce::Random random(7);
    for (int i = 0; i < length; i++) {
        const float x = random.nextFloat() * 2.0f - 1.0f;
        input.setSample(0, i, x);
        input.setSample(1, i, x);
    }
    const auto out = run(filter, input);
    CHECK(Analysis::bandEnergy(out, 8192, sampleRate, 5000.0, 15000.0)
          < 0.01 * Analysis::bandEnergy(input, 8192, sampleRate, 5000.0, 15000.0));
}

TEST_CASE("Compressor reduces loud signals", "[effects]") {
    Compressor compressor;
    set(compressor, "threshold", -20.0f);
    set(compressor, "ratio", 10.0f);
    set(compressor, "makeup", 0.0f);
    const auto out = run(compressor, sine(220.0, 0.9f));
    const double before = Analysis::rms(sine(220.0, 0.9f), 0, 24000, 24000);
    const double after = Analysis::rms(out, 0, 24000, 24000);
    CHECK(after < 0.3 * before);
}

TEST_CASE("Parametric EQ boosts around its frequency", "[effects]") {
    ParametricEq eq;
    set(eq, "freq2", 1000.0f);
    set(eq, "gain2", 12.0f);
    const auto at = [&](double frequency) {
        ParametricEq fresh;
        set(fresh, "freq2", 1000.0f);
        set(fresh, "gain2", 12.0f);
        return Analysis::rms(run(fresh, sine(frequency, 0.1f)), 0, 24000, 24000) / Analysis::rms(sine(frequency, 0.1f), 0, 24000, 24000);
    };
    CHECK_THAT(juce::Decibels::gainToDecibels(at(1000.0)), WithinAbs(12.0, 0.5));
    CHECK_THAT(juce::Decibels::gainToDecibels(at(100.0)), WithinAbs(0.0, 1.0));
}

TEST_CASE("Modulation effects move the sound, and mix 0 is dry", "[effects]") {
    const auto type = GENERATE(as<juce::String> {}, "Chorus", "Flanger", "Phaser");
    CAPTURE(type);
    const auto dry = sine(440.0);

    auto wet = EffectRegistry::create(type);
    CHECK(maxDifference(run(*wet, dry), dry, 4800) > 0.05f);

    // JUCE smooths the mix control from its prepare-time value, so measure once it has settled.
    auto off = EffectRegistry::create(type);
    set(*off, "mix", 0.0f);
    CHECK(maxDifference(run(*off, dry), dry, 24000) < 1.0e-4f);
}

TEST_CASE("Effects run between the machine and its channel, in slot order", "[effects][session]") {
    MachineRig rig(SubtractiveSynth::typeName);
    rig.set("osc1Shape", 3.0f);
    rig.set("oscMix", 0.0f);
    rig.set("cutoff", 20000.0f);
    rig.set("filterEnvAmount", 0.0f);

    REQUIRE(rig.session.setEffect(rig.machine, 0, FilterEffect::typeName));
    REQUIRE(rig.session.setEffectParameter(rig.machine, 0, "cutoff", 100.0f)); // low-pass far below the note
    const auto filtered = rig.play({ { 0.0, 4.0, 81, 0.8f } }, 0.4);
    rig.render(48000);

    REQUIRE(rig.session.setEffect(rig.machine, 0, {})); // remove it
    rig.render(512);
    const auto clean = rig.play({ { 0.0, 4.0, 81, 0.8f } }, 0.4);

    CHECK(Analysis::rms(filtered, 0, 4800, 9600) < 0.05 * Analysis::rms(clean, 0, 4800, 9600));
    CHECK(rig.session.getEffectParameters(rig.machine, 0) == nullptr);
    CHECK_FALSE(rig.session.setEffect(rig.machine, 2, Chorus::typeName));
    CHECK_FALSE(rig.session.setEffect(rig.machine, 0, "NoSuchEffect"));
    CHECK_FALSE(rig.session.setEffectParameter(rig.machine, 1, "mix", 0.5f)); // empty slot
}
