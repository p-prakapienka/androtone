#include "DrumKit.h"

#include <cmath>

namespace {
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    juce::AudioBuffer<float> makeBuffer(double seconds) {
        juce::AudioBuffer<float> buffer(1, static_cast<int>(seconds * DrumKit::sampleRate));
        buffer.clear();
        return buffer;
    }

    double expDecay(double t, double time) { return std::exp(-t / time); }

    // One-pole filters for shaping noise.
    struct OnePole {
        double state = 0.0;
        double coefficient = 0.0;

        OnePole(double cutoff) {
            coefficient = std::exp(-twoPi * cutoff / DrumKit::sampleRate);
        }
        double lowpass(double x) { return state = (1.0 - coefficient) * x + coefficient * state; }
        double highpass(double x) { return x - lowpass(x); }
    };

    // Pitch-swept sine with an exponential amplitude decay.
    juce::AudioBuffer<float> sweptSine(double seconds, double startHz, double endHz, double sweepTime,
                                       double decayTime, double gain) {
        auto buffer = makeBuffer(seconds);
        double phase = 0.0;
        for (int i = 0; i < buffer.getNumSamples(); i++) {
            const double t = i / DrumKit::sampleRate;
            const double frequency = endHz + (startHz - endHz) * expDecay(t, sweepTime);
            phase += frequency / DrumKit::sampleRate;
            buffer.setSample(0, i, static_cast<float>(gain * std::sin(twoPi * phase) * expDecay(t, decayTime)));
        }
        return buffer;
    }

    juce::AudioBuffer<float> kick() {
        auto buffer = sweptSine(0.6, 160.0, 45.0, 0.03, 0.18, 0.95);
        juce::Random random(1);
        for (int i = 0; i < 240; i++) { // a short click on the attack
            buffer.addSample(0, i, static_cast<float>(0.3 * (random.nextDouble() * 2.0 - 1.0) * (1.0 - i / 240.0)));
        }
        return buffer;
    }

    juce::AudioBuffer<float> snare() {
        auto buffer = sweptSine(0.35, 240.0, 180.0, 0.02, 0.06, 0.5);
        juce::Random random(2);
        OnePole highpass(800.0);
        OnePole lowpass(9000.0);
        for (int i = 0; i < buffer.getNumSamples(); i++) {
            const double t = i / DrumKit::sampleRate;
            const double noise = lowpass.lowpass(highpass.highpass(random.nextDouble() * 2.0 - 1.0));
            buffer.addSample(0, i, static_cast<float>(0.7 * noise * expDecay(t, 0.09)));
        }
        return buffer;
    }

    juce::AudioBuffer<float> hat(double seconds, double decayTime, int seed) {
        auto buffer = makeBuffer(seconds);
        juce::Random random(seed);
        OnePole highpass1(6000.0);
        OnePole highpass2(6000.0);
        for (int i = 0; i < buffer.getNumSamples(); i++) {
            const double t = i / DrumKit::sampleRate;
            const double noise = highpass2.highpass(highpass1.highpass(random.nextDouble() * 2.0 - 1.0));
            buffer.setSample(0, i, static_cast<float>(0.8 * noise * expDecay(t, decayTime)));
        }
        return buffer;
    }

    juce::AudioBuffer<float> clap() {
        auto buffer = makeBuffer(0.4);
        juce::Random random(5);
        OnePole highpass(900.0);
        OnePole lowpass(3500.0);
        for (int i = 0; i < buffer.getNumSamples(); i++) {
            const double t = i / DrumKit::sampleRate;
            // Three quick bursts, then the tail.
            double envelope = 0.0;
            for (const double burst : { 0.0, 0.011, 0.022 }) {
                if (t >= burst) {
                    envelope = juce::jmax(envelope, expDecay(t - burst, burst < 0.02 ? 0.006 : 0.11));
                }
            }
            const double noise = lowpass.lowpass(highpass.highpass(random.nextDouble() * 2.0 - 1.0));
            buffer.setSample(0, i, static_cast<float>(1.4 * noise * envelope));
        }
        return buffer;
    }

    juce::AudioBuffer<float> cowbell() {
        auto buffer = makeBuffer(0.4);
        double phase1 = 0.0;
        double phase2 = 0.0;
        OnePole highpass(400.0);
        for (int i = 0; i < buffer.getNumSamples(); i++) {
            const double t = i / DrumKit::sampleRate;
            phase1 += 540.0 / DrumKit::sampleRate;
            phase2 += 800.0 / DrumKit::sampleRate;
            const double square = (std::fmod(phase1, 1.0) < 0.5 ? 1.0 : -1.0) + (std::fmod(phase2, 1.0) < 0.5 ? 1.0 : -1.0);
            const double envelope = 0.6 * expDecay(t, 0.02) + 0.4 * expDecay(t, 0.12);
            buffer.setSample(0, i, static_cast<float>(0.25 * highpass.highpass(square) * envelope));
        }
        return buffer;
    }
}

const std::array<const char*, DrumKit::numSounds>& DrumKit::names() {
    static const std::array<const char*, numSounds> kitNames {
        "Kick", "Snare", "Closed Hat", "Open Hat", "Clap", "Low Tom", "High Tom", "Cowbell"
    };
    return kitNames;
}

std::array<juce::AudioBuffer<float>, DrumKit::numSounds> DrumKit::generate() {
    return {
        kick(),
        snare(),
        hat(0.15, 0.025, 3),
        hat(0.7, 0.2, 4),
        clap(),
        sweptSine(0.6, 130.0, 90.0, 0.05, 0.16, 0.8),
        sweptSine(0.5, 220.0, 160.0, 0.05, 0.12, 0.7),
        cowbell(),
    };
}
