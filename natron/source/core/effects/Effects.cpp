#include "Effects.h"

#include <cmath>
#include <map>

namespace {
    juce::dsp::ProcessSpec stereoSpec(double sampleRate, int maxBlockSize) {
        return { sampleRate, static_cast<juce::uint32>(juce::jmax(1, maxBlockSize)), 2 };
    }

    juce::dsp::AudioBlock<float> blockOf(juce::AudioBuffer<float>& buffer, int numSamples) {
        return juce::dsp::AudioBlock<float>(buffer).getSubBlock(0, static_cast<size_t>(numSamples));
    }
}

// ------------------------------------------------------------------------------------------------ Distortion

Distortion::Distortion() {
    parameters.add({ "drive", "Drive", 0.0f, 40.0f, 18.0f }); // dB
    parameters.add({ "tone", "Tone", 200.0f, 20000.0f, 6000.0f });
    parameters.add({ "mix", "Mix", 0.0f, 1.0f, 1.0f });
    parameters.add({ "output", "Output", -24.0f, 6.0f, -6.0f }); // dB
    jassert(parameters.size() == numParameters);
}

void Distortion::prepare(double newSampleRate, int) {
    sampleRate = newSampleRate;
    reset();
}

void Distortion::process(juce::AudioBuffer<float>& buffer, int numSamples) {
    const float driveGain = juce::Decibels::decibelsToGain(parameters.get(drive));
    const float outputGain = juce::Decibels::decibelsToGain(parameters.get(output));
    const float wet = parameters.get(mix);
    const auto coefficient = static_cast<float>(
        std::exp(-juce::MathConstants<double>::twoPi * parameters.get(tone) / sampleRate));

    for (int ch = 0; ch < 2; ch++) {
        float* data = buffer.getWritePointer(ch);
        float s = state[static_cast<size_t>(ch)];
        for (int i = 0; i < numSamples; i++) {
            const float shaped = std::tanh(data[i] * driveGain);
            s = shaped + coefficient * (s - shaped);
            data[i] = (data[i] + wet * (s - data[i])) * outputGain;
        }
        state[static_cast<size_t>(ch)] = s;
    }
}

// ------------------------------------------------------------------------------------------------ Bitcrusher

Bitcrusher::Bitcrusher() {
    parameters.add({ "bits", "Bits", 1.0f, 16.0f, 6.0f, 1.0f });
    parameters.add({ "downsample", "Downsample", 1.0f, 32.0f, 4.0f, 1.0f });
    parameters.add({ "mix", "Mix", 0.0f, 1.0f, 1.0f });
    jassert(parameters.size() == numParameters);
}

void Bitcrusher::process(juce::AudioBuffer<float>& buffer, int numSamples) {
    const float levels = std::pow(2.0f, juce::roundToInt(parameters.get(bits)) - 1.0f);
    const int hold = juce::jmax(1, juce::roundToInt(parameters.get(downsample)));
    const float wet = parameters.get(mix);
    float* left = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);

    for (int i = 0; i < numSamples; i++) {
        if (counter++ % hold == 0) {
            held[0] = std::round(left[i] * levels) / levels;
            held[1] = std::round(right[i] * levels) / levels;
        }
        left[i] += wet * (held[0] - left[i]);
        right[i] += wet * (held[1] - right[i]);
    }
}

// ------------------------------------------------------------------------------------------------ Chorus / Flanger

void ModulatedDelay::prepare(double sampleRate, int maxBlockSize) {
    chorus.prepare(stereoSpec(sampleRate, maxBlockSize));
}

void ModulatedDelay::process(juce::AudioBuffer<float>& buffer, int numSamples) {
    chorus.setRate(parameters.get(rate));
    chorus.setDepth(parameters.get(depth));
    chorus.setCentreDelay(parameters.get(delay));
    chorus.setFeedback(parameters.get(feedback));
    chorus.setMix(parameters.get(mix));
    auto block = blockOf(buffer, numSamples);
    chorus.process(juce::dsp::ProcessContextReplacing<float>(block));
}

Chorus::Chorus() {
    parameters.add({ "rate", "Rate", 0.05f, 5.0f, 0.8f }); // Hz
    parameters.add({ "depth", "Depth", 0.0f, 1.0f, 0.4f });
    parameters.add({ "delay", "Delay", 1.0f, 30.0f, 8.0f }); // ms
    parameters.add({ "feedback", "Feedback", -0.95f, 0.95f, 0.0f });
    parameters.add({ "mix", "Mix", 0.0f, 1.0f, 0.5f });
    jassert(parameters.size() == numParameters);
}

Flanger::Flanger() {
    parameters.add({ "rate", "Rate", 0.02f, 2.0f, 0.25f });
    parameters.add({ "depth", "Depth", 0.0f, 1.0f, 0.8f });
    parameters.add({ "delay", "Delay", 1.0f, 8.0f, 2.0f });
    parameters.add({ "feedback", "Feedback", -0.95f, 0.95f, 0.6f });
    parameters.add({ "mix", "Mix", 0.0f, 1.0f, 0.5f });
    jassert(parameters.size() == numParameters);
}

// ------------------------------------------------------------------------------------------------ Phaser

Phaser::Phaser() {
    parameters.add({ "rate", "Rate", 0.02f, 5.0f, 0.5f });
    parameters.add({ "depth", "Depth", 0.0f, 1.0f, 0.7f });
    parameters.add({ "centre", "Centre", 100.0f, 5000.0f, 1000.0f }); // Hz
    parameters.add({ "feedback", "Feedback", -0.95f, 0.95f, 0.5f });
    parameters.add({ "mix", "Mix", 0.0f, 1.0f, 0.5f });
    jassert(parameters.size() == numParameters);
}

void Phaser::prepare(double sampleRate, int maxBlockSize) {
    phaser.prepare(stereoSpec(sampleRate, maxBlockSize));
}

void Phaser::process(juce::AudioBuffer<float>& buffer, int numSamples) {
    phaser.setRate(parameters.get(rate));
    phaser.setDepth(parameters.get(depth));
    phaser.setCentreFrequency(parameters.get(centre));
    phaser.setFeedback(parameters.get(feedback));
    phaser.setMix(parameters.get(mix));
    auto block = blockOf(buffer, numSamples);
    phaser.process(juce::dsp::ProcessContextReplacing<float>(block));
}

// ------------------------------------------------------------------------------------------------ Compressor

Compressor::Compressor() {
    parameters.add({ "threshold", "Threshold", -60.0f, 0.0f, -18.0f }); // dB
    parameters.add({ "ratio", "Ratio", 1.0f, 20.0f, 4.0f });
    parameters.add({ "attack", "Attack", 0.1f, 100.0f, 5.0f }); // ms
    parameters.add({ "release", "Release", 10.0f, 1000.0f, 120.0f }); // ms
    parameters.add({ "makeup", "Makeup", 0.0f, 24.0f, 6.0f }); // dB
    jassert(parameters.size() == numParameters);
}

void Compressor::prepare(double sampleRate, int maxBlockSize) {
    compressor.prepare(stereoSpec(sampleRate, maxBlockSize));
}

void Compressor::process(juce::AudioBuffer<float>& buffer, int numSamples) {
    compressor.setThreshold(parameters.get(threshold));
    compressor.setRatio(parameters.get(ratio));
    compressor.setAttack(parameters.get(attack));
    compressor.setRelease(parameters.get(release));
    auto block = blockOf(buffer, numSamples);
    compressor.process(juce::dsp::ProcessContextReplacing<float>(block));
    block.multiplyBy(juce::Decibels::decibelsToGain(parameters.get(makeup)));
}

// ------------------------------------------------------------------------------------------------ Filter

FilterEffect::FilterEffect() {
    parameters.add({ "type", "Type", 0.0f, 2.0f, 0.0f, 1.0f }); // low, high, band
    parameters.add({ "cutoff", "Cutoff", 20.0f, 20000.0f, 1000.0f });
    parameters.add({ "resonance", "Resonance", 0.0f, 1.0f, 0.3f });
    jassert(parameters.size() == numParameters);
}

void FilterEffect::prepare(double sampleRate, int maxBlockSize) {
    filter.prepare(stereoSpec(sampleRate, maxBlockSize));
}

void FilterEffect::process(juce::AudioBuffer<float>& buffer, int numSamples) {
    switch (juce::roundToInt(parameters.get(type))) {
        case 1: filter.setType(juce::dsp::StateVariableTPTFilterType::highpass); break;
        case 2: filter.setType(juce::dsp::StateVariableTPTFilterType::bandpass); break;
        default: filter.setType(juce::dsp::StateVariableTPTFilterType::lowpass); break;
    }
    filter.setCutoffFrequency(parameters.get(cutoff));
    filter.setResonance(juce::jmap(parameters.get(resonance), 0.5f, 10.0f));
    auto block = blockOf(buffer, numSamples);
    filter.process(juce::dsp::ProcessContextReplacing<float>(block));
}

// ------------------------------------------------------------------------------------------------ Parametric EQ

ParametricEq::ParametricEq() {
    const float defaultFrequencies[numBands] = { 200.0f, 1000.0f, 5000.0f };
    for (int band = 0; band < numBands; band++) {
        const juce::String n = juce::String(band + 1);
        parameters.add({ "freq" + n, "Freq " + n, 20.0f, 20000.0f, defaultFrequencies[band] });
        parameters.add({ "gain" + n, "Gain " + n, -18.0f, 18.0f, 0.0f }); // dB
        parameters.add({ "q" + n, "Q " + n, 0.1f, 10.0f, 1.0f });
    }
}

void ParametricEq::prepare(double newSampleRate, int) {
    sampleRate = newSampleRate;
    reset();
}

void ParametricEq::reset() {
    for (auto& band : bands) {
        band.reset();
    }
    for (auto& values : applied) {
        values = { -1.0f, -1.0f, -1.0f }; // force a coefficient update
    }
}

void ParametricEq::process(juce::AudioBuffer<float>& buffer, int numSamples) {
    bool anyActive = false;
    for (int band = 0; band < numBands; band++) {
        const std::array<float, parametersPerBand> values {
            parameters.get(band * parametersPerBand + frequency), parameters.get(band * parametersPerBand + gain),
            parameters.get(band * parametersPerBand + q)
        };
        auto& previous = applied[static_cast<size_t>(band)];
        if (values != previous) {
            bands[static_cast<size_t>(band)].setPeak(sampleRate, values[frequency], values[q], values[gain]);
            previous = values;
        }
        anyActive = anyActive || !juce::exactlyEqual(values[gain], 0.0f);
    }
    if (!anyActive) {
        return;
    }
    for (int ch = 0; ch < 2; ch++) {
        float* data = buffer.getWritePointer(ch);
        for (int i = 0; i < numSamples; i++) {
            float x = data[i];
            for (auto& band : bands) {
                x = band.process(ch, x);
            }
            data[i] = x;
        }
    }
}

// ------------------------------------------------------------------------------------------------ Registry

namespace {
    using Factory = std::unique_ptr<Effect> (*)();

    const std::map<juce::String, Factory>& factories() {
        static const std::map<juce::String, Factory> registered {
            { Distortion::typeName, [] () -> std::unique_ptr<Effect> { return std::make_unique<Distortion>(); } },
            { Bitcrusher::typeName, [] () -> std::unique_ptr<Effect> { return std::make_unique<Bitcrusher>(); } },
            { Chorus::typeName, [] () -> std::unique_ptr<Effect> { return std::make_unique<Chorus>(); } },
            { Flanger::typeName, [] () -> std::unique_ptr<Effect> { return std::make_unique<Flanger>(); } },
            { Phaser::typeName, [] () -> std::unique_ptr<Effect> { return std::make_unique<Phaser>(); } },
            { Compressor::typeName, [] () -> std::unique_ptr<Effect> { return std::make_unique<Compressor>(); } },
            { FilterEffect::typeName, [] () -> std::unique_ptr<Effect> { return std::make_unique<FilterEffect>(); } },
            { ParametricEq::typeName, [] () -> std::unique_ptr<Effect> { return std::make_unique<ParametricEq>(); } },
        };
        return registered;
    }
}

juce::StringArray EffectRegistry::getTypeNames() {
    juce::StringArray names;
    for (const auto& entry : factories()) {
        names.add(entry.first);
    }
    return names;
}

std::unique_ptr<Effect> EffectRegistry::create(const juce::String& typeName) {
    const auto it = factories().find(typeName);
    return it != factories().end() ? it->second() : nullptr;
}
