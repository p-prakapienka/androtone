#pragma once

// Signal measurements for behaviour tests: pitch, spectral centroid, onset, band energy.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>

namespace Analysis {
    // Mono mix of `length` samples from `start` (clamped to the buffer).
    inline std::vector<float> mono(const juce::AudioBuffer<float>& buffer, int start, int length) {
        start = juce::jlimit(0, buffer.getNumSamples(), start);
        length = juce::jlimit(0, buffer.getNumSamples() - start, length);
        std::vector<float> result(static_cast<size_t>(length), 0.0f);
        for (int ch = 0; ch < buffer.getNumChannels(); ch++) {
            for (int i = 0; i < length; i++) {
                result[static_cast<size_t>(i)] += buffer.getSample(ch, start + i) / static_cast<float>(buffer.getNumChannels());
            }
        }
        return result;
    }

    // Fundamental frequency by normalised autocorrelation, searched between minHz and maxHz.
    inline double pitch(const juce::AudioBuffer<float>& buffer, int start, double sampleRate, int length = 4096,
                        double minHz = 30.0, double maxHz = 2000.0) {
        const auto x = mono(buffer, start, length);
        const int minLag = static_cast<int>(sampleRate / maxHz);
        const int maxLag = juce::jmin(static_cast<int>(sampleRate / minHz), static_cast<int>(x.size()) / 2);

        std::vector<double> r(static_cast<size_t>(maxLag + 2), 0.0);
        for (int lag = minLag; lag <= maxLag + 1; lag++) {
            double sum = 0.0, energyA = 0.0, energyB = 0.0;
            for (size_t i = 0; i + static_cast<size_t>(lag) < x.size(); i++) {
                sum += x[i] * x[i + static_cast<size_t>(lag)];
                energyA += x[i] * x[i];
                energyB += x[i + static_cast<size_t>(lag)] * x[i + static_cast<size_t>(lag)];
            }
            r[static_cast<size_t>(lag)] = sum / std::sqrt(energyA * energyB + 1.0e-12);
        }

        // The first lag whose correlation comes within 10% of the best avoids octave errors.
        double best = 0.0;
        for (int lag = minLag; lag <= maxLag; lag++) {
            best = std::max(best, r[static_cast<size_t>(lag)]);
        }
        int chosen = minLag;
        for (int lag = minLag + 1; lag < maxLag; lag++) {
            const double v = r[static_cast<size_t>(lag)];
            if (v >= 0.9 * best && v >= r[static_cast<size_t>(lag - 1)] && v >= r[static_cast<size_t>(lag + 1)]) {
                chosen = lag;
                break;
            }
        }
        // Parabolic interpolation around the peak.
        const double a = r[static_cast<size_t>(chosen - 1)], b = r[static_cast<size_t>(chosen)], c = r[static_cast<size_t>(chosen + 1)];
        const double denominator = a - 2.0 * b + c;
        const double offset = std::abs(denominator) > 1.0e-12 ? 0.5 * (a - c) / denominator : 0.0;
        return sampleRate / (chosen + offset);
    }

    inline double semitonesBetween(double a, double b) { return 12.0 * std::log2(a / b); }

    // Magnitude spectrum of a Hann-windowed 4096-sample frame.
    inline std::vector<float> spectrum(const juce::AudioBuffer<float>& buffer, int start) {
        constexpr int order = 12;
        constexpr int size = 1 << order;
        const auto x = mono(buffer, start, size);
        std::vector<float> data(static_cast<size_t>(2 * size), 0.0f);
        for (size_t i = 0; i < x.size(); i++) {
            const double window = 0.5 - 0.5 * std::cos(juce::MathConstants<double>::twoPi * static_cast<double>(i) / (size - 1));
            data[i] = static_cast<float>(x[i] * window);
        }
        juce::dsp::FFT fft(order);
        fft.performFrequencyOnlyForwardTransform(data.data());
        data.resize(static_cast<size_t>(size / 2));
        return data;
    }

    inline double centroid(const juce::AudioBuffer<float>& buffer, int start, double sampleRate) {
        const auto magnitudes = spectrum(buffer, start);
        const double binHz = sampleRate / (2.0 * static_cast<double>(magnitudes.size()));
        double weighted = 0.0, total = 0.0;
        for (size_t i = 1; i < magnitudes.size(); i++) {
            weighted += static_cast<double>(i) * binHz * magnitudes[i];
            total += magnitudes[i];
        }
        return total > 1.0e-9 ? weighted / total : 0.0;
    }

    // Energy between two frequencies, from one frame.
    inline double bandEnergy(const juce::AudioBuffer<float>& buffer, int start, double sampleRate, double lowHz, double highHz) {
        const auto magnitudes = spectrum(buffer, start);
        const double binHz = sampleRate / (2.0 * static_cast<double>(magnitudes.size()));
        double energy = 0.0;
        for (size_t i = 0; i < magnitudes.size(); i++) {
            const double frequency = static_cast<double>(i) * binHz;
            if (frequency >= lowHz && frequency < highHz) {
                energy += static_cast<double>(magnitudes[i]) * magnitudes[i];
            }
        }
        return energy;
    }

    // First sample whose absolute value (either channel) exceeds `threshold`, or -1.
    inline int onset(const juce::AudioBuffer<float>& buffer, float threshold, int from = 0) {
        for (int i = from; i < buffer.getNumSamples(); i++) {
            for (int ch = 0; ch < buffer.getNumChannels(); ch++) {
                if (std::abs(buffer.getSample(ch, i)) > threshold) {
                    return i;
                }
            }
        }
        return -1;
    }

    inline double rms(const juce::AudioBuffer<float>& buffer, int channel, int start, int length) {
        start = juce::jlimit(0, buffer.getNumSamples(), start);
        length = juce::jlimit(0, buffer.getNumSamples() - start, length);
        return length > 0 ? buffer.getRMSLevel(channel, start, length) : 0.0;
    }
}
