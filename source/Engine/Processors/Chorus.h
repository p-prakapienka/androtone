#pragma once

#include "Processor.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

// Stereo chorus implemented as two short, continuously modulated delay lines.
// Unlike the send-style Delay, this is an insert effect: process() blends the
// dry input with the modulated signal. The right LFO is 90 degrees out of phase
// with the left one to create stereo width.
class Chorus : public Processor {
public:
    Chorus() {
        rateHz.store(0.8f);
        depthMs.store(4.0f);
        centreDelayMs.store(8.0f);
        feedback.store(0.1f);
        mix.store(0.5f);
    }

    void prepare(double newSampleRate) override {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        const auto bufferSize = static_cast<size_t>(
            std::ceil(maxDelayMs * 0.001 * sampleRate)) + 2;
        bufferLeft.assign(bufferSize, 0.0f);
        bufferRight.assign(bufferSize, 0.0f);
        writePos = 0;
        lfoPhase = 0.0;
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

        const float leftDelayMs = centre + depth * lfo(lfoPhase);
        const float rightDelayMs = centre + depth * lfo(wrapPhase(lfoPhase + 0.25));
        const float wetLeft = readInterpolated(bufferLeft, leftDelayMs);
        const float wetRight = readInterpolated(bufferRight, rightDelayMs);

        const float feedbackAmount = feedback.load();
        bufferLeft[writePos] = dryLeft + wetLeft * feedbackAmount;
        bufferRight[writePos] = dryRight + wetRight * feedbackAmount;

        const float wetMix = mix.load();
        left = dryLeft + (wetLeft - dryLeft) * wetMix;
        right = dryRight + (wetRight - dryRight) * wetMix;

        writePos = (writePos + 1) % bufferLeft.size();
        lfoPhase = wrapPhase(lfoPhase + rateHz.load() / sampleRate);
    }

    void reset() override {
        std::fill(bufferLeft.begin(), bufferLeft.end(), 0.0f);
        std::fill(bufferRight.begin(), bufferRight.end(), 0.0f);
        writePos = 0;
        lfoPhase = 0.0;
    }

    void setRateHz(float value) { rateHz.store(std::clamp(value, 0.01f, 10.0f)); }
    void setDepthMs(float value) {
        const float centre = centreDelayMs.load();
        const float availableDepth = std::min(centre - minDelayMs, maxDelayMs - centre);
        depthMs.store(std::clamp(value, 0.0f,
                                 std::min(maxDepthMs, std::max(0.0f, availableDepth))));
    }
    void setCentreDelayMs(float value) {
        centreDelayMs.store(std::clamp(value, minDelayMs + depthMs.load(),
                                       maxDelayMs - depthMs.load()));
    }
    void setFeedback(float value) { feedback.store(std::clamp(value, -0.95f, 0.95f)); }
    void setMix(float value) { mix.store(std::clamp(value, 0.0f, 1.0f)); }

    float getRateHz() const { return rateHz.load(); }
    float getDepthMs() const { return depthMs.load(); }
    float getCentreDelayMs() const { return centreDelayMs.load(); }
    float getFeedback() const { return feedback.load(); }
    float getMix() const { return mix.load(); }

private:
    static constexpr float minDelayMs = 0.1f;
    static constexpr float maxDelayMs = 50.0f;
    static constexpr float maxDepthMs = 20.0f;
    static constexpr double twoPi = 6.28318530717958647692;

    static double wrapPhase(double phase) {
        return phase >= 1.0 ? phase - std::floor(phase) : phase;
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
