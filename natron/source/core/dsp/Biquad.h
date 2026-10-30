#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

// A stereo biquad (RBJ cookbook) with allocation-free coefficient updates, for EQs that change
// on the audio thread. Transposed direct form II.
class Biquad {
public:
    void reset() {
        for (auto& s : state) {
            s = {};
        }
    }

    void setLowShelf(double sampleRate, double frequency, double gainDb) {
        const double a = std::pow(10.0, gainDb / 40.0);
        const double w = omega(sampleRate, frequency);
        const double alpha = std::sin(w) / 2.0 * std::sqrt(2.0); // shelf slope 1
        const double cosW = std::cos(w);
        const double rootA = 2.0 * std::sqrt(a) * alpha;
        set(a * ((a + 1) - (a - 1) * cosW + rootA), 2 * a * ((a - 1) - (a + 1) * cosW),
            a * ((a + 1) - (a - 1) * cosW - rootA), (a + 1) + (a - 1) * cosW + rootA,
            -2 * ((a - 1) + (a + 1) * cosW), (a + 1) + (a - 1) * cosW - rootA);
    }

    void setHighShelf(double sampleRate, double frequency, double gainDb) {
        const double a = std::pow(10.0, gainDb / 40.0);
        const double w = omega(sampleRate, frequency);
        const double alpha = std::sin(w) / 2.0 * std::sqrt(2.0);
        const double cosW = std::cos(w);
        const double rootA = 2.0 * std::sqrt(a) * alpha;
        set(a * ((a + 1) + (a - 1) * cosW + rootA), -2 * a * ((a - 1) + (a + 1) * cosW),
            a * ((a + 1) + (a - 1) * cosW - rootA), (a + 1) - (a - 1) * cosW + rootA,
            2 * ((a - 1) - (a + 1) * cosW), (a + 1) - (a - 1) * cosW - rootA);
    }

    void setPeak(double sampleRate, double frequency, double q, double gainDb) {
        const double a = std::pow(10.0, gainDb / 40.0);
        const double w = omega(sampleRate, frequency);
        const double alpha = std::sin(w) / (2.0 * q);
        const double cosW = std::cos(w);
        set(1 + alpha * a, -2 * cosW, 1 - alpha * a, 1 + alpha / a, -2 * cosW, 1 - alpha / a);
    }

    // Constant 0 dB peak band-pass.
    void setBandPass(double sampleRate, double frequency, double q) {
        const double w = omega(sampleRate, frequency);
        const double alpha = std::sin(w) / (2.0 * q);
        const double cosW = std::cos(w);
        set(alpha, 0.0, -alpha, 1 + alpha, -2 * cosW, 1 - alpha);
    }

    // Second-order high-pass (Butterworth at q = 0.707).
    void setHighPass(double sampleRate, double frequency, double q = 0.7071) {
        const double w = omega(sampleRate, frequency);
        const double alpha = std::sin(w) / (2.0 * q);
        const double cosW = std::cos(w);
        set((1 + cosW) / 2, -(1 + cosW), (1 + cosW) / 2, 1 + alpha, -2 * cosW, 1 - alpha);
    }

    // Second-order low-pass (Butterworth at q = 0.707).
    void setLowPass(double sampleRate, double frequency, double q = 0.7071) {
        const double w = omega(sampleRate, frequency);
        const double alpha = std::sin(w) / (2.0 * q);
        const double cosW = std::cos(w);
        set((1 - cosW) / 2, 1 - cosW, (1 - cosW) / 2, 1 + alpha, -2 * cosW, 1 - alpha);
    }

    float process(int channel, float x) {
        auto& s = state[channel];
        const float y = b0 * x + s.z1;
        s.z1 = b1 * x - a1 * y + s.z2;
        s.z2 = b2 * x - a2 * y;
        return y;
    }

private:
    struct State {
        float z1 = 0.0f;
        float z2 = 0.0f;
    };

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    State state[2];

    static double omega(double sampleRate, double frequency) {
        return juce::MathConstants<double>::twoPi * juce::jlimit(10.0, sampleRate * 0.45, frequency) / sampleRate;
    }

    void set(double nb0, double nb1, double nb2, double na0, double na1, double na2) {
        b0 = static_cast<float>(nb0 / na0);
        b1 = static_cast<float>(nb1 / na0);
        b2 = static_cast<float>(nb2 / na0);
        a1 = static_cast<float>(na1 / na0);
        a2 = static_cast<float>(na2 / na0);
    }
};
