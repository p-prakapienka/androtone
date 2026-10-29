#pragma once

#include "Effect.h"
#include "../dsp/Biquad.h"

#include <juce_dsp/juce_dsp.h>
#include <array>

// The insert effects (spec section 5). Parameter ids are saved in song files: never rename one.

// Soft-clipping overdrive with a tone (low-pass) control.
class Distortion : public Effect {
public:
    static constexpr const char* typeName = "Distortion";
    enum ParameterIndex { drive, tone, mix, output, numParameters };

    Distortion();
    juce::String getTypeName() const override { return typeName; }
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(juce::AudioBuffer<float>& buffer, int numSamples) override;
    void reset() override { state = {}; }

private:
    double sampleRate = 48000.0;
    std::array<float, 2> state {};
};

// Bit-depth and sample-rate reduction.
class Bitcrusher : public Effect {
public:
    static constexpr const char* typeName = "Bitcrusher";
    enum ParameterIndex { bits, downsample, mix, numParameters };

    Bitcrusher();
    juce::String getTypeName() const override { return typeName; }
    void prepare(double, int) override { reset(); }
    void process(juce::AudioBuffer<float>& buffer, int numSamples) override;
    void reset() override { held = {}; counter = 0; }

private:
    std::array<float, 2> held {};
    int counter = 0;
};

// Chorus and Flanger: juce::dsp::Chorus with different ranges.
class ModulatedDelay : public Effect {
public:
    enum ParameterIndex { rate, depth, delay, feedback, mix, numParameters };

    void prepare(double sampleRate, int maxBlockSize) override;
    void process(juce::AudioBuffer<float>& buffer, int numSamples) override;
    void reset() override { chorus.reset(); }

protected:
    juce::dsp::Chorus<float> chorus;
};

class Chorus : public ModulatedDelay {
public:
    static constexpr const char* typeName = "Chorus";
    Chorus();
    juce::String getTypeName() const override { return typeName; }
};

class Flanger : public ModulatedDelay {
public:
    static constexpr const char* typeName = "Flanger";
    Flanger();
    juce::String getTypeName() const override { return typeName; }
};

class Phaser : public Effect {
public:
    static constexpr const char* typeName = "Phaser";
    enum ParameterIndex { rate, depth, centre, feedback, mix, numParameters };

    Phaser();
    juce::String getTypeName() const override { return typeName; }
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(juce::AudioBuffer<float>& buffer, int numSamples) override;
    void reset() override { phaser.reset(); }

private:
    juce::dsp::Phaser<float> phaser;
};

class Compressor : public Effect {
public:
    static constexpr const char* typeName = "Compressor";
    enum ParameterIndex { threshold, ratio, attack, release, makeup, numParameters };

    Compressor();
    juce::String getTypeName() const override { return typeName; }
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(juce::AudioBuffer<float>& buffer, int numSamples) override;
    void reset() override { compressor.reset(); }

private:
    juce::dsp::Compressor<float> compressor;
};

// Resonant low/high/band-pass.
class FilterEffect : public Effect {
public:
    static constexpr const char* typeName = "Filter";
    enum ParameterIndex { type, cutoff, resonance, numParameters };

    FilterEffect();
    juce::String getTypeName() const override { return typeName; }
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(juce::AudioBuffer<float>& buffer, int numSamples) override;
    void reset() override { filter.reset(); }

private:
    juce::dsp::StateVariableTPTFilter<float> filter;
};

// Three peaking bands, each with frequency, gain and Q.
class ParametricEq : public Effect {
public:
    static constexpr const char* typeName = "Parametric EQ";
    static constexpr int numBands = 3;
    enum BandParameter { frequency, gain, q, parametersPerBand };

    ParametricEq();
    juce::String getTypeName() const override { return typeName; }
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(juce::AudioBuffer<float>& buffer, int numSamples) override;
    void reset() override;

private:
    double sampleRate = 48000.0;
    std::array<Biquad, numBands> bands;
    std::array<std::array<float, parametersPerBand>, numBands> applied {};
};
