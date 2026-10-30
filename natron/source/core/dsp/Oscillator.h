#pragma once

#include "FastSine.h"

#include <juce_core/juce_core.h>
#include <cmath>

// Band-limited (PolyBLEP) oscillator: saw, square, triangle, sine. Phase in [0, 1).
class Oscillator {
public:
    enum class Shape { saw, square, triangle, sine };

    static Shape shapeFromParameter(float value) {
        return static_cast<Shape>(juce::jlimit(0, 3, juce::roundToInt(value)));
    }

    void reset(double startPhase = 0.0) {
        phase = startPhase;
        triangleState = 0.0;
    }

    // frequency / sampleRate
    float next(Shape shape, double increment) {
        increment = juce::jlimit(0.0, 0.5, increment);
        double value = 0.0;

        switch (shape) {
            case Shape::saw:
                value = 2.0 * phase - 1.0 - polyBlep(phase, increment);
                break;
            case Shape::square:
                value = squareAt(increment);
                break;
            case Shape::triangle:
                // Integrated band-limited square, leaky to stay centred.
                triangleState = 0.999 * triangleState + 4.0 * increment * squareAt(increment);
                value = triangleState;
                break;
            case Shape::sine:
                value = FastSine::ofCycles(phase);
                break;
        }

        phase += increment;
        if (phase >= 1.0) {
            phase -= 1.0;
        }
        return static_cast<float>(value);
    }

private:
    double phase = 0.0;
    double triangleState = 0.0;

    double squareAt(double increment) const {
        double value = phase < 0.5 ? 1.0 : -1.0;
        value += polyBlep(phase, increment);
        value -= polyBlep(std::fmod(phase + 0.5, 1.0), increment);
        return value;
    }

    static double polyBlep(double t, double dt) {
        if (dt <= 0.0) {
            return 0.0;
        }
        if (t < dt) {
            t /= dt;
            return t + t - t * t - 1.0;
        }
        if (t > 1.0 - dt) {
            t = (t - 1.0) / dt;
            return t * t + t + t + 1.0;
        }
        return 0.0;
    }
};
