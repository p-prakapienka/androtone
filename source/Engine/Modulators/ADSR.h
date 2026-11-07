#pragma once

#include <algorithm>
#include <cmath>

// A simple linear attack-decay-sustain-release envelope generator. Call
// getNextValue() once per sample and use its 0..1 output to modulate a sound
// source or processor parameter.
class ADSR {
public:
    void prepare(double newSampleRate) {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        reset();
    }

    void noteOn() {
        state = State::attack;
        updateIncrement(1.0f, attackSeconds);
    }

    void noteOff() {
        if (state == State::idle) {
            return;
        }

        state = State::release;
        updateIncrement(0.0f, releaseSeconds);
    }

    float getNextValue() {
        while (state != State::idle) {
            if (state == State::sustain) {
                currentValue = sustainLevel;
                return currentValue;
            }

            if (samplesRemaining > 0) {
                currentValue += increment;
                --samplesRemaining;

                if (samplesRemaining == 0) {
                    currentValue = targetValue;
                    advanceState();
                }

                return std::clamp(currentValue, 0.0f, 1.0f);
            }

            currentValue = targetValue;
            advanceState();
        }

        return 0.0f;
    }

    void reset() {
        state = State::idle;
        currentValue = 0.0f;
        targetValue = 0.0f;
        increment = 0.0f;
        samplesRemaining = 0;
    }

    void setAttackSeconds(float value) { attackSeconds = std::max(0.0f, value); }
    void setDecaySeconds(float value) { decaySeconds = std::max(0.0f, value); }
    void setSustainLevel(float value) { sustainLevel = std::clamp(value, 0.0f, 1.0f); }
    void setReleaseSeconds(float value) { releaseSeconds = std::max(0.0f, value); }

    float getAttackSeconds() const { return attackSeconds; }
    float getDecaySeconds() const { return decaySeconds; }
    float getSustainLevel() const { return sustainLevel; }
    float getReleaseSeconds() const { return releaseSeconds; }
    float getCurrentValue() const { return currentValue; }
    bool isActive() const { return state != State::idle; }

private:
    enum class State { idle, attack, decay, sustain, release };

    void updateIncrement(float newTargetValue, float durationSeconds) {
        targetValue = newTargetValue;
        samplesRemaining = static_cast<int>(std::round(durationSeconds * sampleRate));
        increment = samplesRemaining > 0
            ? (targetValue - currentValue) / static_cast<float>(samplesRemaining)
            : 0.0f;
    }

    void advanceState() {
        switch (state) {
            case State::attack:
                state = State::decay;
                updateIncrement(sustainLevel, decaySeconds);
                break;
            case State::decay:
                state = State::sustain;
                targetValue = sustainLevel;
                increment = 0.0f;
                samplesRemaining = 0;
                break;
            case State::sustain:
                break;
            case State::release:
                reset();
                break;
            case State::idle:
                break;
        }
    }

    double sampleRate = 44100.0;
    float attackSeconds = 0.01f;
    float decaySeconds = 0.1f;
    float sustainLevel = 0.8f;
    float releaseSeconds = 0.2f;
    float currentValue = 0.0f;
    float targetValue = 0.0f;
    float increment = 0.0f;
    int samplesRemaining = 0;
    State state = State::idle;
};
