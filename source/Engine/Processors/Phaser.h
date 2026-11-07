#pragma once

#include "Processor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

// Stereo four-stage phaser. Each channel has an independent all-pass cascade,
// while a shared sine LFO sweeps both cascades together. Unlike the send
// effects, Phaser mixes dry and wet internally and is intended as an insert.
// Parameter setters are lock-free so they can be driven by the UI thread.
class Phaser : public Processor {
public:
    Phaser() {
        rateHz.store(0.5f);
        centreFrequencyHz.store(800.0f);
        depth.store(0.75f);
        feedback.store(0.25f);
        mix.store(0.5f);
    }

    void prepare(double newSampleRate) override {
        sampleRate = std::max(newSampleRate, 1.0);
        reset();
    }

    void process(float& left, float& right) override {
        const float currentRate = std::clamp(rateHz.load(), minRateHz, maxRateHz);
        lfoPhase += twoPi * static_cast<double>(currentRate) / sampleRate;
        if (lfoPhase >= twoPi) {
            lfoPhase -= twoPi;
        }

        const float lfo = static_cast<float>(std::sin(lfoPhase));
        const float currentDepth = std::clamp(depth.load(), 0.0f, 1.0f);
        const float centre = std::clamp(centreFrequencyHz.load(), minFrequencyHz,
                                        getMaximumFrequency());
        const float sweepOctaves = maxSweepOctaves * currentDepth;
        const float frequency = std::clamp(
            centre * std::pow(2.0f, lfo * sweepOctaves), minFrequencyHz,
            getMaximumFrequency());
        const float coefficient = calculateAllPassCoefficient(frequency);

        const float currentFeedback = std::clamp(feedback.load(), -0.95f, 0.95f);
        const float wetLeft = processChannel(left + feedbackLeft * currentFeedback,
                                             coefficient, leftStages);
        const float wetRight = processChannel(right + feedbackRight * currentFeedback,
                                              coefficient, rightStages);
        feedbackLeft = wetLeft;
        feedbackRight = wetRight;

        const float wetMix = std::clamp(mix.load(), 0.0f, 1.0f);
        left += (wetLeft - left) * wetMix;
        right += (wetRight - right) * wetMix;
    }

    void reset() override {
        leftStages = {};
        rightStages = {};
        feedbackLeft = 0.0f;
        feedbackRight = 0.0f;
        lfoPhase = 0.0;
    }

    void setRateHz(float value) { rateHz.store(std::clamp(value, minRateHz, maxRateHz)); }
    void setCentreFrequencyHz(float value) {
        centreFrequencyHz.store(std::clamp(value, minFrequencyHz, getMaximumFrequency()));
    }
    void setDepth(float value) { depth.store(std::clamp(value, 0.0f, 1.0f)); }
    void setFeedback(float value) { feedback.store(std::clamp(value, -0.95f, 0.95f)); }
    void setMix(float value) { mix.store(std::clamp(value, 0.0f, 1.0f)); }

    float getRateHz() const { return rateHz.load(); }
    float getCentreFrequencyHz() const { return centreFrequencyHz.load(); }
    float getDepth() const { return depth.load(); }
    float getFeedback() const { return feedback.load(); }
    float getMix() const { return mix.load(); }

private:
    struct AllPassStage {
        float previousInput = 0.0f;
        float previousOutput = 0.0f;
    };

    static constexpr size_t stageCount = 4;
    static constexpr float minRateHz = 0.01f;
    static constexpr float maxRateHz = 10.0f;
    static constexpr float minFrequencyHz = 20.0f;
    static constexpr float maxSweepOctaves = 2.0f;
    static constexpr double twoPi = 6.28318530717958647692;

    float getMaximumFrequency() const {
        return static_cast<float>(std::max(20.0, sampleRate * 0.45));
    }

    float calculateAllPassCoefficient(float frequency) const {
        const float tangent = std::tan(static_cast<float>(3.14159265358979323846)
                                       * frequency / static_cast<float>(sampleRate));
        return (1.0f - tangent) / (1.0f + tangent);
    }

    static float processChannel(float input, float coefficient,
                                std::array<AllPassStage, stageCount>& stages) {
        float output = input;
        for (auto& stage : stages) {
            const float stageOutput = coefficient * (output - stage.previousOutput)
                                      + stage.previousInput;
            stage.previousInput = output;
            stage.previousOutput = stageOutput;
            output = stageOutput;
        }
        return output;
    }

    double sampleRate = 44100.0;
    double lfoPhase = 0.0;
    std::array<AllPassStage, stageCount> leftStages{};
    std::array<AllPassStage, stageCount> rightStages{};
    float feedbackLeft = 0.0f;
    float feedbackRight = 0.0f;

    std::atomic<float> rateHz;
    std::atomic<float> centreFrequencyHz;
    std::atomic<float> depth;
    std::atomic<float> feedback;
    std::atomic<float> mix;
};
