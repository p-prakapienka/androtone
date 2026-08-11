#pragma once

#include "Processor.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

// Stereo feedback delay with fractional-delay interpolation and optional
// ping-pong feedback. It defaults to a 100%-wet signal because the mixer uses
// it as a send return, but the mix control also allows it to be used as an
// insert effect. Buffers are allocated in prepare(); process() is lock-free and
// allocation-free.
class Delay : public Processor {
public:
    Delay() {
        delaySeconds.store(0.375f);
        feedback.store(0.4f);
        mix.store(1.0f);
        pingPong.store(false);
    }

    void prepare(double newSampleRate) override {
        sampleRate = std::max(newSampleRate, 1.0);
        const auto bufferSize = static_cast<std::size_t>(
            std::ceil(static_cast<double>(maxDelaySeconds) * sampleRate)) + 2;
        bufferLeft.assign(bufferSize, 0.0f);
        bufferRight.assign(bufferSize, 0.0f);
        currentDelaySamples = getTargetDelaySamples();
        delaySmoothingCoefficient = static_cast<float>(std::exp(
            -1.0 / (delaySmoothingSeconds * sampleRate)));
        reset();
    }

    void process(float& left, float& right) override {
        if (bufferLeft.empty()) {
            return;
        }

        const float dryLeft = left;
        const float dryRight = right;
        const double targetDelaySamples = getTargetDelaySamples();
        currentDelaySamples = targetDelaySamples
                              + delaySmoothingCoefficient
                                    * (currentDelaySamples - targetDelaySamples);

        const float delayedLeft = readInterpolated(bufferLeft);
        const float delayedRight = readInterpolated(bufferRight);
        const float feedbackAmount = feedback.load();

        if (pingPong.load()) {
            bufferLeft[writePosition] = removeDenormal(
                dryLeft + delayedRight * feedbackAmount);
            bufferRight[writePosition] = removeDenormal(
                dryRight + delayedLeft * feedbackAmount);
        } else {
            bufferLeft[writePosition] = removeDenormal(
                dryLeft + delayedLeft * feedbackAmount);
            bufferRight[writePosition] = removeDenormal(
                dryRight + delayedRight * feedbackAmount);
        }

        const float wetMix = mix.load();
        left = dryLeft + (delayedLeft - dryLeft) * wetMix;
        right = dryRight + (delayedRight - dryRight) * wetMix;

        writePosition = (writePosition + 1) % bufferLeft.size();
    }

    void reset() override {
        std::fill(bufferLeft.begin(), bufferLeft.end(), 0.0f);
        std::fill(bufferRight.begin(), bufferRight.end(), 0.0f);
        writePosition = 0;
        currentDelaySamples = getTargetDelaySamples();
    }

    void setDelaySeconds(float value) {
        delaySeconds.store(std::clamp(value, minDelaySeconds, maxDelaySeconds));
    }

    void setDelayTimeMs(float value) { setDelaySeconds(value * 0.001f); }

    void setFeedback(float value) {
        feedback.store(std::clamp(value, minFeedback, maxFeedback));
    }

    void setMix(float value) { mix.store(std::clamp(value, 0.0f, 1.0f)); }
    void setPingPong(bool shouldPingPong) { pingPong.store(shouldPingPong); }

    float getDelaySeconds() const { return delaySeconds.load(); }
    float getDelayTimeMs() const { return getDelaySeconds() * 1000.0f; }
    float getFeedback() const { return feedback.load(); }
    float getMix() const { return mix.load(); }
    bool isPingPong() const { return pingPong.load(); }

private:
    static constexpr float minDelaySeconds = 0.001f;
    static constexpr float maxDelaySeconds = 2.0f;
    static constexpr float minFeedback = -0.95f;
    static constexpr float maxFeedback = 0.95f;
    static constexpr double delaySmoothingSeconds = 0.02;

    double getTargetDelaySamples() const {
        if (bufferLeft.size() < 3) {
            return std::max(1.0,
                            static_cast<double>(delaySeconds.load()) * sampleRate);
        }
        return std::clamp(static_cast<double>(delaySeconds.load()) * sampleRate,
                          1.0, static_cast<double>(bufferLeft.size() - 2));
    }

    float readInterpolated(const std::vector<float>& buffer) const {
        double readPosition = static_cast<double>(writePosition)
                              - currentDelaySamples;
        if (readPosition < 0.0) {
            readPosition += static_cast<double>(buffer.size());
        }

        const auto first = static_cast<std::size_t>(readPosition);
        const auto second = (first + 1) % buffer.size();
        const float fraction = static_cast<float>(
            readPosition - static_cast<double>(first));
        return buffer[first] + (buffer[second] - buffer[first]) * fraction;
    }

    static float removeDenormal(float value) {
        return std::abs(value) < 1.0e-20f ? 0.0f : value;
    }

    double sampleRate = 44100.0;
    std::vector<float> bufferLeft;
    std::vector<float> bufferRight;
    std::size_t writePosition = 0;
    double currentDelaySamples = 1.0;
    float delaySmoothingCoefficient = 0.0f;

    std::atomic<float> delaySeconds;
    std::atomic<float> feedback;
    std::atomic<float> mix;
    std::atomic<bool> pingPong;
};
