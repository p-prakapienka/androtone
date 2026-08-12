#pragma once

#include "Processor.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

// Stereo flanger implemented with two short, fractionally-read delay lines.
// The right LFO is 90 degrees out of phase with the left one for stereo width.
// Flanger is an insert effect and mixes the dry and delayed signals internally.
// Parameter setters are lock-free so they can be driven by the UI thread.
class Flanger : public Processor {
public:
    Flanger() {
        rateHz.store(0.25f);
        depthMs.store(1.5f);
        centreDelayMs.store(2.0f);
        feedback.store(0.5f);
        mix.store(0.5f);
    }

    void prepare(double newSampleRate) override {
        sampleRate = std::max(newSampleRate, 1.0);
        const auto bufferSize = static_cast<size_t>(
            std::ceil(maxDelayMs * 0.001 * sampleRate)) + 2;
        bufferLeft.assign(bufferSize, 0.0f);
        bufferRight.assign(bufferSize, 0.0f);
        reset();
    }

    void process(float& left, float& right) override {
        if (bufferLeft.empty()) {
            return;
        }

        const float dryLeft = left;
        const float dryRight = right;
        const float centre = centreDelayMs.load();
        const float availableDepth = std::min(centre - minDelayMs, maxDelayMs - centre);
        const float depth = std::min(depthMs.load(), std::max(0.0f, availableDepth));

        const float delayedLeft = readInterpolated(
            bufferLeft, centre + depth * lfo(lfoPhase));
        const float delayedRight = readInterpolated(
            bufferRight, centre + depth * lfo(wrapPhase(lfoPhase + stereoPhaseOffset)));

        const float feedbackAmount = feedback.load();
        bufferLeft[writePos] = dryLeft + delayedLeft * feedbackAmount;
        bufferRight[writePos] = dryRight + delayedRight * feedbackAmount;

        const float wetMix = mix.load();
        left = dryLeft + (delayedLeft - dryLeft) * wetMix;
        right = dryRight + (delayedRight - dryRight) * wetMix;

        writePos = (writePos + 1) % bufferLeft.size();
        lfoPhase = wrapPhase(lfoPhase + rateHz.load() / sampleRate);
    }

    void reset() override {
        std::fill(bufferLeft.begin(), bufferLeft.end(), 0.0f);
        std::fill(bufferRight.begin(), bufferRight.end(), 0.0f);
        writePos = 0;
        lfoPhase = 0.0;
    }

    void setRateHz(float value) {
        rateHz.store(std::clamp(value, minRateHz, maxRateHz));
    }

    void setDepthMs(float value) {
        const float centre = centreDelayMs.load();
        const float availableDepth = std::min(centre - minDelayMs, maxDelayMs - centre);
        depthMs.store(std::clamp(value, 0.0f, std::max(0.0f, availableDepth)));
    }

    void setCentreDelayMs(float value) {
        const float depth = depthMs.load();
        centreDelayMs.store(std::clamp(value, minDelayMs + depth, maxDelayMs - depth));
    }

    void setFeedback(float value) {
        feedback.store(std::clamp(value, -maxFeedback, maxFeedback));
    }

    void setMix(float value) {
        mix.store(std::clamp(value, 0.0f, 1.0f));
    }

    float getRateHz() const { return rateHz.load(); }
    float getDepthMs() const { return depthMs.load(); }
    float getCentreDelayMs() const { return centreDelayMs.load(); }
    float getFeedback() const { return feedback.load(); }
    float getMix() const { return mix.load(); }

private:
    static constexpr float minRateHz = 0.01f;
    static constexpr float maxRateHz = 10.0f;
    static constexpr float minDelayMs = 0.1f;
    static constexpr float maxDelayMs = 10.0f;
    static constexpr float maxFeedback = 0.95f;
    static constexpr double stereoPhaseOffset = 0.25;
    static constexpr double twoPi = 6.28318530717958647692;

    static double wrapPhase(double phase) {
        return phase - std::floor(phase);
    }

    static float lfo(double phase) {
        return static_cast<float>(std::sin(twoPi * phase));
    }

    float readInterpolated(const std::vector<float>& buffer, float delayMs) const {
        const double delaySamples = std::clamp(
            static_cast<double>(delayMs) * 0.001 * sampleRate,
            1.0, static_cast<double>(buffer.size() - 2));
        double readPosition = static_cast<double>(writePos) - delaySamples;
        while (readPosition < 0.0) {
            readPosition += static_cast<double>(buffer.size());
        }

        const auto first = static_cast<size_t>(readPosition);
        const auto second = (first + 1) % buffer.size();
        const float fraction = static_cast<float>(readPosition - static_cast<double>(first));
        return buffer[first] + (buffer[second] - buffer[first]) * fraction;
    }

    double sampleRate = 44100.0;
    std::vector<float> bufferLeft;
    std::vector<float> bufferRight;
    size_t writePos = 0;
    double lfoPhase = 0.0;

    std::atomic<float> rateHz;
    std::atomic<float> depthMs;
    std::atomic<float> centreDelayMs;
    std::atomic<float> feedback;
    std::atomic<float> mix;
};
