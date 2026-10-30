#pragma once

#include "Effect.h"
#include "../dsp/Biquad.h"

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

// The rest of the spec's insert effects. Parameter ids are saved in song files: never rename one.

// Envelope-following filter sweep.
class AutoWah : public Effect {
public:
    static constexpr const char* typeName = "AutoWah";
    enum ParameterIndex { sensitivity, baseFrequency, range, resonance, attack, release, mix, numParameters };
    AutoWah();
    juce::String getTypeName() const override { return typeName; }
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(juce::AudioBuffer<float>& buffer, int numSamples) override;
    void reset() override;

private:
    double sampleRate = 48000.0;
    juce::dsp::StateVariableTPTFilter<float> filter;
    float envelope = 0.0f;
};

// Peak limiter with makeup gain: instant attack, so peaks never pass threshold + makeup.
class Limiter : public Effect {
public:
    static constexpr const char* typeName = "Limiter";
    enum ParameterIndex { threshold, release, makeup, numParameters };
    Limiter();
    juce::String getTypeName() const override { return typeName; }
    void prepare(double newSampleRate, int) override { sampleRate = newSampleRate; reset(); }
    void process(juce::AudioBuffer<float>& buffer, int numSamples) override;
    void reset() override { gain = 1.0f; }

private:
    double sampleRate = 48000.0;
    float gain = 1.0f;
};

// Record wear: crackle, hiss, wow, and a narrowed bandwidth.
class Vinyl : public Effect {
public:
    static constexpr const char* typeName = "Vinyl";
    enum ParameterIndex { crackle, hiss, wow, tone, mix, numParameters };
    Vinyl();
    juce::String getTypeName() const override { return typeName; }
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(juce::AudioBuffer<float>& buffer, int numSamples) override;
    void reset() override;

private:
    double sampleRate = 48000.0;
    juce::Random random { 1977 };
    std::vector<float> line; // wow delay line, both channels interleaved
    int write = 0;
    double wowPhase = 0.0;
    Biquad lowPass;
    Biquad highPass;
    float lastTone = -1.0f;
    float crackleState = 0.0f;
};

// Feedback comb shared by Comb (tuned in Hz) and Static Flanger (set in ms).
class CombBase : public Effect {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(juce::AudioBuffer<float>& buffer, int numSamples) override;
    void reset() override;

protected:
    virtual double delaySeconds() const = 0;
    virtual float feedbackAmount() const = 0;
    virtual float dampingAmount() const = 0;
    virtual float mixAmount() const = 0;

private:
    double sampleRate = 48000.0;
    std::array<std::vector<float>, 2> lines;
    std::array<float, 2> damped {};
    int write = 0;
};

class Comb : public CombBase {
public:
    static constexpr const char* typeName = "Comb";
    enum ParameterIndex { frequency, feedback, damping, mix, numParameters };
    Comb();
    juce::String getTypeName() const override { return typeName; }

protected:
    double delaySeconds() const override { return 1.0 / parameters.get(frequency); }
    float feedbackAmount() const override { return parameters.get(feedback); }
    float dampingAmount() const override { return parameters.get(damping); }
    float mixAmount() const override { return parameters.get(mix); }
};

class StaticFlanger : public CombBase {
public:
    static constexpr const char* typeName = "Static Flanger";
    enum ParameterIndex { delay, feedback, mix, numParameters };
    StaticFlanger();
    juce::String getTypeName() const override { return typeName; }

protected:
    double delaySeconds() const override { return parameters.get(delay) / 1000.0; }
    float feedbackAmount() const override { return parameters.get(feedback); }
    float dampingAmount() const override { return 0.0f; }
    float mixAmount() const override { return parameters.get(mix); }
};

// Speaker cabinet voicings (fixed EQ curves) with a tone control.
class Cabinet : public Effect {
public:
    static constexpr const char* typeName = "Cabinet";
    enum ParameterIndex { type, tone, mix, numParameters };
    Cabinet();
    juce::String getTypeName() const override { return typeName; }
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(juce::AudioBuffer<float>& buffer, int numSamples) override;
    void reset() override;

private:
    double sampleRate = 48000.0;
    std::array<Biquad, 4> stages;
    int appliedType = -1;
    float appliedTone = -1.0f;
    void configure(int cabinet, float toneValue);
};

// Analog-style octave divider: one and two octaves below, from zero crossings.
class Octaver : public Effect {
public:
    static constexpr const char* typeName = "Octaver";
    enum ParameterIndex { octaveDown, twoOctavesDown, dry, tone, numParameters };
    Octaver();
    juce::String getTypeName() const override { return typeName; }
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(juce::AudioBuffer<float>& buffer, int numSamples) override;
    void reset() override;

private:
    double sampleRate = 48000.0;
    Biquad tracking;  // low-passes the input before zero-crossing detection
    Biquad smoothing; // tones down the square-wave output
    float envelope = 0.0f;
    float previous = 0.0f;
    float divideBy2 = 1.0f;
    float divideBy4 = 1.0f;
    float lastTone = -1.0f;
};

// Pitch wobble from a modulated delay (fully wet).
class Vibrato : public Effect {
public:
    static constexpr const char* typeName = "Vibrato";
    enum ParameterIndex { rate, depth, numParameters };
    Vibrato();
    juce::String getTypeName() const override { return typeName; }
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(juce::AudioBuffer<float>& buffer, int numSamples) override;
    void reset() override;

private:
    double sampleRate = 48000.0;
    std::array<std::vector<float>, 2> lines;
    int write = 0;
    double phase = 0.0;
};

// LFO amplitude modulation.
class Tremolo : public Effect {
public:
    static constexpr const char* typeName = "Tremolo";
    enum ParameterIndex { rate, depth, shape, numParameters };
    Tremolo();
    juce::String getTypeName() const override { return typeName; }
    void prepare(double newSampleRate, int) override { sampleRate = newSampleRate; reset(); }
    void process(juce::AudioBuffer<float>& buffer, int numSamples) override;
    void reset() override { phase = 0.0; }

private:
    double sampleRate = 48000.0;
    double phase = 0.0;
};

// LFO stereo panning.
class AutoPan : public Effect {
public:
    static constexpr const char* typeName = "AutoPan";
    enum ParameterIndex { rate, depth, shape, numParameters };
    AutoPan();
    juce::String getTypeName() const override { return typeName; }
    void prepare(double newSampleRate, int) override { sampleRate = newSampleRate; reset(); }
    void process(juce::AudioBuffer<float>& buffer, int numSamples) override;
    void reset() override { phase = 0.0; }

private:
    double sampleRate = 48000.0;
    double phase = 0.0;
};
