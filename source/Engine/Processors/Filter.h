#pragma once

#include "Processor.h"

#include <algorithm>
#include <atomic>
#include <cmath>

// Stereo multimode biquad filter with low-pass, high-pass, band-pass and notch
// responses. Cutoff, resonance and mode can be updated lock-free from the UI
// thread; coefficients are recalculated on the audio thread only when a
// parameter actually changes.
class Filter : public Processor {
public:
    enum class Type {
        LowPass,
        HighPass,
        BandPass,
        Notch
    };

    Filter() {
        cutoffHz.store(1000.0f);
        resonance.store(0.7071f);
        type.store(Type::LowPass);
    }

    void prepare(double newSampleRate) override {
        sampleRate = newSampleRate;
        cachedCutoffHz = -1.0f;
        cachedResonance = -1.0f;
        updateCoefficientsIfNeeded();
        reset();
    }

    void process(float& left, float& right) override {
        updateCoefficientsIfNeeded();
        left = processSample(left, leftState);
        right = processSample(right, rightState);
    }

    void reset() override {
        leftState = {};
        rightState = {};
    }

    void setType(Type newType) {
        type.store(newType);
    }

    void setCutoffHz(float newCutoffHz) {
        cutoffHz.store(newCutoffHz);
    }

    void setResonance(float newResonance) {
        resonance.store(newResonance);
    }

    Type getType() const { return type.load(); }
    float getCutoffHz() const { return cutoffHz.load(); }
    float getResonance() const { return resonance.load(); }

private:
    struct State {
        float x1 = 0.0f;
        float x2 = 0.0f;
        float y1 = 0.0f;
        float y2 = 0.0f;
    };

    void updateCoefficientsIfNeeded() {
        const Type newType = type.load();
        const float maxCutoffHz = static_cast<float>(sampleRate * 0.45);
        const float newCutoffHz = std::clamp(cutoffHz.load(), 20.0f, maxCutoffHz);
        const float newResonance = std::clamp(resonance.load(), 0.1f, 20.0f);

        if (newType == cachedType
            && newCutoffHz == cachedCutoffHz
            && newResonance == cachedResonance) {
            return;
        }

        cachedType = newType;
        cachedCutoffHz = newCutoffHz;
        cachedResonance = newResonance;

        constexpr float pi = 3.14159265358979323846f;
        const float omega = 2.0f * pi * newCutoffHz / static_cast<float>(sampleRate);
        const float sinOmega = std::sin(omega);
        const float cosOmega = std::cos(omega);
        const float alpha = sinOmega / (2.0f * newResonance);

        float rawB0 = 0.0f;
        float rawB1 = 0.0f;
        float rawB2 = 0.0f;
        const float rawA0 = 1.0f + alpha;
        const float rawA1 = -2.0f * cosOmega;
        const float rawA2 = 1.0f - alpha;

        switch (newType) {
            case Type::LowPass:
                rawB0 = (1.0f - cosOmega) * 0.5f;
                rawB1 = 1.0f - cosOmega;
                rawB2 = rawB0;
                break;
            case Type::HighPass:
                rawB0 = (1.0f + cosOmega) * 0.5f;
                rawB1 = -(1.0f + cosOmega);
                rawB2 = rawB0;
                break;
            case Type::BandPass:
                rawB0 = alpha;
                rawB1 = 0.0f;
                rawB2 = -alpha;
                break;
            case Type::Notch:
                rawB0 = 1.0f;
                rawB1 = -2.0f * cosOmega;
                rawB2 = 1.0f;
                break;
        }

        b0 = rawB0 / rawA0;
        b1 = rawB1 / rawA0;
        b2 = rawB2 / rawA0;
        a1 = rawA1 / rawA0;
        a2 = rawA2 / rawA0;
    }

    float processSample(float input, State& state) const {
        const float output = b0 * input
                           + b1 * state.x1
                           + b2 * state.x2
                           - a1 * state.y1
                           - a2 * state.y2;

        state.x2 = state.x1;
        state.x1 = input;
        state.y2 = state.y1;
        state.y1 = output;
        return output;
    }

    double sampleRate = 44100.0;

    std::atomic<Type> type;
    std::atomic<float> cutoffHz;
    std::atomic<float> resonance;

    Type cachedType = Type::LowPass;
    float cachedCutoffHz = -1.0f;
    float cachedResonance = -1.0f;

    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;

    State leftState;
    State rightState;
};
