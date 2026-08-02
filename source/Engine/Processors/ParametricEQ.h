#pragma once

#include "Processor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Five-stage stereo equaliser: a high-pass filter, three parametric peak bands,
// and a low-pass filter. The high-pass and low-pass filters are second-order
// (12 dB/octave) stages. Parameter setters only update atomics; coefficient
// changes are picked up by the audio thread without locks or allocations.
class ParametricEQ : public Processor {
public:
    enum class Band : std::size_t {
        Low = 0,
        Mid,
        High
    };

    ParametricEQ() {
        highPass.frequencyHz.store(20.0f);
        highPass.q.store(butterworthQ);
        highPass.enabled.store(false);

        bands[bandIndex(Band::Low)].frequencyHz.store(200.0f);
        bands[bandIndex(Band::Mid)].frequencyHz.store(1000.0f);
        bands[bandIndex(Band::High)].frequencyHz.store(5000.0f);
        for (auto& band : bands) {
            band.gainDb.store(0.0f);
            band.q.store(1.0f);
            band.enabled.store(true);
        }

        lowPass.frequencyHz.store(20000.0f);
        lowPass.q.store(butterworthQ);
        lowPass.enabled.store(false);
    }

    void prepare(double newSampleRate) override {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        reset();
        synchroniseParameters(true);
        prepared = true;
    }

    void process(float& left, float& right) override {
        if (!prepared) {
            return;
        }

        synchroniseParameters(false);

        if (highPassRuntime.active) {
            highPassRuntime.filter.process(left, right);
        }
        for (auto& band : bandRuntimes) {
            if (band.active) {
                band.filter.process(left, right);
            }
        }
        if (lowPassRuntime.active) {
            lowPassRuntime.filter.process(left, right);
        }
    }

    void reset() override {
        highPassRuntime.filter.reset();
        for (auto& band : bandRuntimes) {
            band.filter.reset();
        }
        lowPassRuntime.filter.reset();
    }

    void setHighPassFrequencyHz(float value) {
        highPass.frequencyHz.store(clampFrequency(value));
        markChanged(highPass);
    }
    void setHighPassQ(float value) {
        highPass.q.store(clampQ(value));
        markChanged(highPass);
    }
    void setHighPassEnabled(bool shouldBeEnabled) {
        highPass.enabled.store(shouldBeEnabled);
        markChanged(highPass);
    }

    void setBandFrequencyHz(Band band, float value) {
        auto& parameters = bands[bandIndex(band)];
        parameters.frequencyHz.store(clampFrequency(value));
        markChanged(parameters);
    }
    void setBandGainDb(Band band, float value) {
        auto& parameters = bands[bandIndex(band)];
        parameters.gainDb.store(std::clamp(value, minGainDb, maxGainDb));
        markChanged(parameters);
    }
    void setBandQ(Band band, float value) {
        auto& parameters = bands[bandIndex(band)];
        parameters.q.store(clampQ(value));
        markChanged(parameters);
    }
    void setBandEnabled(Band band, bool shouldBeEnabled) {
        auto& parameters = bands[bandIndex(band)];
        parameters.enabled.store(shouldBeEnabled);
        markChanged(parameters);
    }

    void setLowPassFrequencyHz(float value) {
        lowPass.frequencyHz.store(clampFrequency(value));
        markChanged(lowPass);
    }
    void setLowPassQ(float value) {
        lowPass.q.store(clampQ(value));
        markChanged(lowPass);
    }
    void setLowPassEnabled(bool shouldBeEnabled) {
        lowPass.enabled.store(shouldBeEnabled);
        markChanged(lowPass);
    }

    float getHighPassFrequencyHz() const { return highPass.frequencyHz.load(); }
    float getHighPassQ() const { return highPass.q.load(); }
    bool isHighPassEnabled() const { return highPass.enabled.load(); }

    float getBandFrequencyHz(Band band) const {
        return bands[bandIndex(band)].frequencyHz.load();
    }
    float getBandGainDb(Band band) const {
        return bands[bandIndex(band)].gainDb.load();
    }
    float getBandQ(Band band) const { return bands[bandIndex(band)].q.load(); }
    bool isBandEnabled(Band band) const {
        return bands[bandIndex(band)].enabled.load();
    }

    float getLowPassFrequencyHz() const { return lowPass.frequencyHz.load(); }
    float getLowPassQ() const { return lowPass.q.load(); }
    bool isLowPassEnabled() const { return lowPass.enabled.load(); }

private:
    struct Coefficients {
        double b0 = 1.0;
        double b1 = 0.0;
        double b2 = 0.0;
        double a1 = 0.0;
        double a2 = 0.0;
    };

    class StereoBiquad {
    public:
        void setCoefficients(const Coefficients& newCoefficients) {
            coefficients = newCoefficients;
        }

        void process(float& left, float& right) {
            left = processChannel(left, leftState);
            right = processChannel(right, rightState);
        }

        void reset() {
            leftState = {};
            rightState = {};
        }

    private:
        struct State {
            double x1 = 0.0;
            double x2 = 0.0;
            double y1 = 0.0;
            double y2 = 0.0;
        };

        float processChannel(float input, State& state) {
            const double x = static_cast<double>(input);
            const double y = coefficients.b0 * x
                             + coefficients.b1 * state.x1
                             + coefficients.b2 * state.x2
                             - coefficients.a1 * state.y1
                             - coefficients.a2 * state.y2;
            state.x2 = state.x1;
            state.x1 = x;
            state.y2 = state.y1;
            state.y1 = y;
            return static_cast<float>(y);
        }

        Coefficients coefficients;
        State leftState;
        State rightState;
    };

    struct CutParameters {
        std::atomic<float> frequencyHz;
        std::atomic<float> q;
        std::atomic<bool> enabled;
        std::atomic<std::uint32_t> revision{1};
    };

    struct PeakParameters {
        std::atomic<float> frequencyHz;
        std::atomic<float> gainDb;
        std::atomic<float> q;
        std::atomic<bool> enabled;
        std::atomic<std::uint32_t> revision{1};
    };

    struct Runtime {
        StereoBiquad filter;
        float frequencyHz = 0.0f;
        float gainDb = 0.0f;
        float q = 0.0f;
        bool enabled = false;
        bool active = false;
        std::uint32_t revision = 0;
    };

    static constexpr float butterworthQ = 0.70710678f;
    static constexpr float minFrequencyHz = 10.0f;
    static constexpr float maxFrequencyHz = 40000.0f;
    static constexpr float minQ = 0.1f;
    static constexpr float maxQ = 20.0f;
    static constexpr float minGainDb = -24.0f;
    static constexpr float maxGainDb = 24.0f;
    static constexpr double twoPi = 6.28318530717958647692;

    static constexpr std::size_t bandIndex(Band band) {
        return static_cast<std::size_t>(band);
    }

    template <typename Parameters>
    static void markChanged(Parameters& parameters) {
        parameters.revision.fetch_add(1, std::memory_order_release);
    }

    static float clampFrequency(float value) {
        return std::clamp(value, minFrequencyHz, maxFrequencyHz);
    }

    static float clampQ(float value) {
        return std::clamp(value, minQ, maxQ);
    }

    double effectiveFrequency(float frequencyHz) const {
        return std::clamp(static_cast<double>(frequencyHz),
                          static_cast<double>(minFrequencyHz), sampleRate * 0.49);
    }

    static Coefficients normalise(double b0, double b1, double b2,
                                  double a0, double a1, double a2) {
        return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    }

    Coefficients makeHighPass(float frequencyHz, float q) const {
        const double omega = twoPi * effectiveFrequency(frequencyHz) / sampleRate;
        const double cosine = std::cos(omega);
        const double alpha = std::sin(omega) / (2.0 * static_cast<double>(q));
        return normalise((1.0 + cosine) * 0.5, -(1.0 + cosine),
                         (1.0 + cosine) * 0.5, 1.0 + alpha,
                         -2.0 * cosine, 1.0 - alpha);
    }

    Coefficients makePeak(float frequencyHz, float gainDb, float q) const {
        const double amplitude = std::pow(10.0, static_cast<double>(gainDb) / 40.0);
        const double omega = twoPi * effectiveFrequency(frequencyHz) / sampleRate;
        const double cosine = std::cos(omega);
        const double alpha = std::sin(omega) / (2.0 * static_cast<double>(q));
        return normalise(1.0 + alpha * amplitude, -2.0 * cosine,
                         1.0 - alpha * amplitude, 1.0 + alpha / amplitude,
                         -2.0 * cosine, 1.0 - alpha / amplitude);
    }

    Coefficients makeLowPass(float frequencyHz, float q) const {
        const double omega = twoPi * effectiveFrequency(frequencyHz) / sampleRate;
        const double cosine = std::cos(omega);
        const double alpha = std::sin(omega) / (2.0 * static_cast<double>(q));
        return normalise((1.0 - cosine) * 0.5, 1.0 - cosine,
                         (1.0 - cosine) * 0.5, 1.0 + alpha,
                         -2.0 * cosine, 1.0 - alpha);
    }

    void synchroniseCut(const CutParameters& parameters, Runtime& runtime,
                        bool highPassFilter, bool force) {
        const auto revision = parameters.revision.load(std::memory_order_acquire);
        if (!force && revision == runtime.revision) {
            return;
        }

        const float frequencyHz = parameters.frequencyHz.load();
        const float q = parameters.q.load();
        const bool enabled = parameters.enabled.load();
        const bool activeChanged = enabled != runtime.active;
        runtime.frequencyHz = frequencyHz;
        runtime.q = q;
        runtime.enabled = enabled;
        runtime.active = enabled;
        runtime.filter.setCoefficients(highPassFilter
                                           ? makeHighPass(frequencyHz, q)
                                           : makeLowPass(frequencyHz, q));
        if (activeChanged) {
            runtime.filter.reset();
        }
        runtime.revision = revision;
    }

    void synchronisePeak(const PeakParameters& parameters, Runtime& runtime,
                         bool force) {
        const auto revision = parameters.revision.load(std::memory_order_acquire);
        if (!force && revision == runtime.revision) {
            return;
        }

        const float frequencyHz = parameters.frequencyHz.load();
        const float gainDb = parameters.gainDb.load();
        const float q = parameters.q.load();
        const bool enabled = parameters.enabled.load();
        const bool active = enabled && std::abs(gainDb) > 0.0001f;
        const bool activeChanged = active != runtime.active;
        runtime.frequencyHz = frequencyHz;
        runtime.gainDb = gainDb;
        runtime.q = q;
        runtime.enabled = enabled;
        runtime.active = active;
        runtime.filter.setCoefficients(makePeak(frequencyHz, gainDb, q));
        if (activeChanged) {
            runtime.filter.reset();
        }
        runtime.revision = revision;
    }

    void synchroniseParameters(bool force) {
        synchroniseCut(highPass, highPassRuntime, true, force);
        for (std::size_t i = 0; i < bands.size(); ++i) {
            synchronisePeak(bands[i], bandRuntimes[i], force);
        }
        synchroniseCut(lowPass, lowPassRuntime, false, force);
    }

    double sampleRate = 44100.0;
    bool prepared = false;

    CutParameters highPass;
    std::array<PeakParameters, 3> bands;
    CutParameters lowPass;

    Runtime highPassRuntime;
    std::array<Runtime, 3> bandRuntimes;
    Runtime lowPassRuntime;
};
