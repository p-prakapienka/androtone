#pragma once

#include "Processor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

// Reverb effect. Wraps juce::Reverb, which is the public-domain Freeverb
// algorithm (Jezar at Dreampoint) shipped in juce_audio_basics — no extra
// module needed. Consumes audio and produces audio in place: call prepare()
// once the sample rate is known, then process() on each block. Parameter
// setters are lock-free so the UI thread can update them while audio runs.
class Reverb : public Processor {
public:
    Reverb() {
        roomSize.store(0.5f);
        damping.store(0.5f);
        wetLevel.store(0.33f);
        dryLevel.store(0.4f);
        width.store(1.0f);
        freeze.store(false);
    }

    void prepare(double sampleRate) override {
        reverb.setSampleRate(sampleRate);
        reverb.reset();
        applyParameters();
    }

    void process(float& left, float& right) override {
        reverb.processStereo(&left, &right, 1);
    }

    void reset() override {
        reverb.reset();
    }

    void setRoomSize(float v) {
        roomSize.store(v);
        applyParameters();
    }

    void setDamping(float v) {
        damping.store(v);
        applyParameters();
    }

    void setWetLevel(float v) {
        wetLevel.store(v);
        applyParameters();
    }
    void setDryLevel(float v) {
        dryLevel.store(v);
        applyParameters();
    }
    void setWidth(float v) {
        width.store(v);
        applyParameters();
    }
    void setFreeze(bool shouldFreeze) {
        freeze.store(shouldFreeze);
        applyParameters();
    }

    float getRoomSize() const { return roomSize.load(); }
    float getDamping() const { return damping.load(); }
    float getWetLevel() const { return wetLevel.load(); }
    float getDryLevel() const { return dryLevel.load(); }
    float getWidth() const { return width.load(); }
    bool isFrozen() const { return freeze.load(); }

private:
    void applyParameters() {
        juce::Reverb::Parameters params;
        params.roomSize = roomSize.load();
        params.damping = damping.load();
        params.wetLevel = wetLevel.load();
        params.dryLevel = dryLevel.load();
        params.width = width.load();
        params.freezeMode = freeze.load() ? 1.0f : 0.0f;
        reverb.setParameters(params);
    }

    juce::Reverb reverb;

    std::atomic<float> roomSize;
    std::atomic<float> damping;
    std::atomic<float> wetLevel;
    std::atomic<float> dryLevel;
    std::atomic<float> width;
    std::atomic<bool> freeze;
};
