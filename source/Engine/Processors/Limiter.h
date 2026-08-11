#pragma once

#include "Processor.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// Stereo-linked lookahead peak limiter. The louder channel controls the gain
// applied to both channels, preserving the stereo image. Audio and detector
// buffers are allocated in prepare(); process() is lock-free and allocation-free.
//
// This is a sample-peak limiter rather than a true-peak limiter: it guarantees
// that processed samples do not exceed the threshold, but does not oversample
// to detect inter-sample peaks.
class Limiter : public Processor {
public:
    Limiter() = default;

    void prepare(double newSampleRate) override {
        sampleRate = std::max(newSampleRate, 1.0);
        activeLookaheadSamples = static_cast<std::size_t>(std::round(
            static_cast<double>(lookaheadMs.load()) * 0.001 * sampleRate));

        const std::size_t bufferSize = activeLookaheadSamples + 1;
        delayLeft.assign(bufferSize, 0.0f);
        delayRight.assign(bufferSize, 0.0f);
        requiredGainDelay.assign(bufferSize, 1.0f);
        detectorQueue.resize(bufferSize);

        synchroniseParameters(true);
        reset();
    }

    void process(float& left, float& right) override {
        if (delayLeft.empty()) {
            return;
        }

        synchroniseParameters(false);

        const float peak = std::max(std::abs(left), std::abs(right));
        const float requiredGain = peak > thresholdLinear
                                       ? thresholdLinear / peak
                                       : 1.0f;

        delayLeft[writePosition] = left;
        delayRight[writePosition] = right;
        requiredGainDelay[writePosition] = requiredGain;

        pushDetectorValue(requiredGain);
        const float targetGain = detectorQueue[queueHead].gain;
        const float coefficient = targetGain < envelopeGain
                                      ? attackCoefficient
                                      : releaseCoefficient;
        envelopeGain = targetGain + coefficient * (envelopeGain - targetGain);

        const std::size_t readPosition = (writePosition + 1) % delayLeft.size();
        // The delayed per-sample requirement is a final safety constraint. It
        // keeps the output below the threshold even with zero lookahead or a
        // deliberately slow attack setting.
        const float outputGain = std::min(envelopeGain,
                                          requiredGainDelay[readPosition]);
        left = delayLeft[readPosition] * outputGain;
        right = delayRight[readPosition] * outputGain;

        gainReductionDb.store(outputGain < 1.0f
                                  ? -20.0f * std::log10(outputGain)
                                  : 0.0f,
                              std::memory_order_relaxed);

        writePosition = readPosition;
        ++sampleIndex;
    }

    void reset() override {
        std::fill(delayLeft.begin(), delayLeft.end(), 0.0f);
        std::fill(delayRight.begin(), delayRight.end(), 0.0f);
        std::fill(requiredGainDelay.begin(), requiredGainDelay.end(), 1.0f);
        writePosition = 0;
        queueHead = 0;
        queueSize = 0;
        sampleIndex = 0;
        envelopeGain = 1.0f;
        gainReductionDb.store(0.0f, std::memory_order_relaxed);
    }

    void setThresholdDb(float value) {
        thresholdDb.store(std::clamp(value, minThresholdDb, maxThresholdDb));
        markParametersChanged();
    }

    void setAttackMs(float value) {
        attackMs.store(std::clamp(value, minAttackMs, maxAttackMs));
        markParametersChanged();
    }

    void setReleaseMs(float value) {
        releaseMs.store(std::clamp(value, minReleaseMs, maxReleaseMs));
        markParametersChanged();
    }

    // A lookahead change takes effect on the next prepare() call because it
    // changes the delay-buffer size and therefore the processor latency.
    void setLookaheadMs(float value) {
        lookaheadMs.store(std::clamp(value, minLookaheadMs, maxLookaheadMs));
    }

    float getThresholdDb() const { return thresholdDb.load(); }
    float getAttackMs() const { return attackMs.load(); }
    float getReleaseMs() const { return releaseMs.load(); }
    float getLookaheadMs() const { return lookaheadMs.load(); }
    float getGainReductionDb() const {
        return gainReductionDb.load(std::memory_order_relaxed);
    }
    std::size_t getLatencySamples() const { return activeLookaheadSamples; }

private:
    struct DetectorPoint {
        std::uint64_t index = 0;
        float gain = 1.0f;
    };

    static constexpr float minThresholdDb = -60.0f;
    static constexpr float maxThresholdDb = 0.0f;
    static constexpr float minAttackMs = 0.0f;
    static constexpr float maxAttackMs = 20.0f;
    static constexpr float minReleaseMs = 5.0f;
    static constexpr float maxReleaseMs = 2000.0f;
    static constexpr float minLookaheadMs = 0.0f;
    static constexpr float maxLookaheadMs = 20.0f;

    static float decibelsToGain(float decibels) {
        return std::pow(10.0f, decibels / 20.0f);
    }

    float makeTimeCoefficient(float milliseconds) const {
        if (milliseconds <= 0.0f) {
            return 0.0f;
        }
        return static_cast<float>(std::exp(
            -1.0 / (static_cast<double>(milliseconds) * 0.001 * sampleRate)));
    }

    void markParametersChanged() {
        parameterRevision.fetch_add(1, std::memory_order_release);
    }

    void synchroniseParameters(bool force) {
        const auto revision = parameterRevision.load(std::memory_order_acquire);
        if (!force && revision == appliedParameterRevision) {
            return;
        }

        thresholdLinear = decibelsToGain(thresholdDb.load());
        attackCoefficient = makeTimeCoefficient(attackMs.load());
        releaseCoefficient = makeTimeCoefficient(releaseMs.load());
        appliedParameterRevision = revision;
    }

    void pushDetectorValue(float gain) {
        const std::uint64_t oldestIndex = sampleIndex > activeLookaheadSamples
                                              ? sampleIndex - activeLookaheadSamples
                                              : 0;

        while (queueSize > 0
               && detectorQueue[queueHead].index < oldestIndex) {
            queueHead = (queueHead + 1) % detectorQueue.size();
            --queueSize;
        }

        while (queueSize > 0) {
            const std::size_t back = (queueHead + queueSize - 1)
                                     % detectorQueue.size();
            if (detectorQueue[back].gain < gain) {
                break;
            }
            --queueSize;
        }

        const std::size_t insertionPosition = (queueHead + queueSize)
                                              % detectorQueue.size();
        detectorQueue[insertionPosition] = {sampleIndex, gain};
        ++queueSize;
    }

    double sampleRate = 44100.0;
    std::size_t activeLookaheadSamples = 0;
    std::vector<float> delayLeft;
    std::vector<float> delayRight;
    std::vector<float> requiredGainDelay;
    std::vector<DetectorPoint> detectorQueue;
    std::size_t writePosition = 0;
    std::size_t queueHead = 0;
    std::size_t queueSize = 0;
    std::uint64_t sampleIndex = 0;

    float thresholdLinear = 1.0f;
    float attackCoefficient = 0.0f;
    float releaseCoefficient = 0.0f;
    float envelopeGain = 1.0f;
    std::uint32_t appliedParameterRevision = 0;

    std::atomic<float> thresholdDb{-1.0f};
    std::atomic<float> attackMs{1.0f};
    std::atomic<float> releaseMs{50.0f};
    std::atomic<float> lookaheadMs{5.0f};
    std::atomic<float> gainReductionDb{0.0f};
    std::atomic<std::uint32_t> parameterRevision{1};
};
