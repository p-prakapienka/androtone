#pragma once

#include "Processor.h"

#include <algorithm>
#include <atomic>
#include <vector>

// Delay effect. A simple stereo feedback delay line. Used as a 100%-wet send
// return like Reverb: process() outputs only the delayed (wet) signal, so the
// mixer can fold it into the dry mix scaled per track by the send knobs. The
// delay buffer is sized in prepare() once the sample rate is known. Parameter
// setters are lock-free so the UI thread can update them while audio runs.
class Delay : public Processor {
public:
    Delay() {
        delaySeconds.store(0.375f);
        feedback.store(0.4f);
    }

    void prepare(double newSampleRate) override {
        sampleRate = newSampleRate;
        const int maxSamples = static_cast<int>(maxDelaySeconds * sampleRate) + 1;
        bufferLeft.assign(static_cast<size_t>(maxSamples), 0.0f);
        bufferRight.assign(static_cast<size_t>(maxSamples), 0.0f);
        writePos = 0;
    }

    void process(float& left, float& right) override {
        if (bufferLeft.empty()) {
            return;
        }

        const int size = static_cast<int>(bufferLeft.size());
        int delaySamples = static_cast<int>(delaySeconds.load() * sampleRate);
        delaySamples = std::clamp(delaySamples, 1, size - 1);

        int readPos = writePos - delaySamples;
        if (readPos < 0) {
            readPos += size;
        }

        const float delayedLeft = bufferLeft[static_cast<size_t>(readPos)];
        const float delayedRight = bufferRight[static_cast<size_t>(readPos)];

        const float fb = feedback.load();
        bufferLeft[static_cast<size_t>(writePos)] = left + delayedLeft * fb;
        bufferRight[static_cast<size_t>(writePos)] = right + delayedRight * fb;

        writePos++;
        if (writePos >= size) {
            writePos = 0;
        }

        // Wet-only output (send return): emit just the delayed signal.
        left = delayedLeft;
        right = delayedRight;
    }

    void reset() override {
        std::fill(bufferLeft.begin(), bufferLeft.end(), 0.0f);
        std::fill(bufferRight.begin(), bufferRight.end(), 0.0f);
        writePos = 0;
    }

    void setDelaySeconds(float v) {
        delaySeconds.store(v);
    }

    void setFeedback(float v) {
        feedback.store(v);
    }

    float getDelaySeconds() const { return delaySeconds.load(); }
    float getFeedback() const { return feedback.load(); }

private:
    static constexpr float maxDelaySeconds = 2.0f;

    double sampleRate = 44100.0;
    std::vector<float> bufferLeft;
    std::vector<float> bufferRight;
    int writePos = 0;

    std::atomic<float> delaySeconds;
    std::atomic<float> feedback;
};
