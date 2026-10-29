#pragma once

// Building blocks shared by the audio effects. Memory is only allocated in prepare() methods;
// everything else is allocation- and lock-free and safe on the audio thread.

namespace rb::fx
{

constexpr float pi = juce::MathConstants<float>::pi;
constexpr float twoPi = juce::MathConstants<float>::twoPi;

// Effects render in short chunks so that expensive coefficient updates (tan, pow) run at a
// reduced rate while parameters are moving.
constexpr int chunkSize = 16;

// Parameter i of a value array, or the fallback when missing or not finite.
inline float param (const float* values, int numValues, int index, float fallback)
{
    if (values == nullptr || index < 0 || index >= numValues)
        return fallback;
    const float v = values[index];
    return std::isfinite (v) ? v : fallback;
}

inline int choice (float value, int numChoices)
{
    return juce::jlimit (0, numChoices - 1, juce::roundToInt (value));
}

// Rational tanh approximation, exact at 0, saturates at +-1 from |x| = 3.
inline float fastTanh (float x)
{
    if (x >= 3.0f) return 1.0f;
    if (x <= -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// Linear up to |x| = 1, then saturates smoothly towards |x| = 2. Used in feedback paths.
inline float softLimit (float x)
{
    const float a = std::abs (x);
    if (a <= 1.0f)
        return x;
    const float y = 1.0f + fastTanh (a - 1.0f);
    return x < 0.0f ? -y : y;
}

inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }

inline float onePoleCoeff (double sampleRate, double cutoffHz)
{
    return (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * cutoffHz / sampleRate));
}

inline float timeCoeff (double sampleRate, double seconds)
{
    return (float) (1.0 - std::exp (-1.0 / (sampleRate * juce::jmax (1.0e-5, seconds))));
}

//==============================================================================
// Exponential parameter smoother. Tracks in double precision so it always lands exactly on its
// target (important for "mix = 0 is exactly dry").
class Smoothed
{
public:
    void prepare (double sampleRate, double timeSeconds)
    {
        coeff = 1.0 - std::exp (-1.0 / (sampleRate * juce::jmax (1.0e-5, timeSeconds)));
    }

    void setTarget (float t) { target = t; }
    void snap (float v) { current = target = v; }
    void snapToTarget() { current = target; }

    float getTarget() const { return (float) target; }
    float get() const { return (float) current; }
    bool isSmoothing() const { return ! juce::exactlyEqual (current, target); }

    float next()
    {
        const double d = target - current;
        if (std::abs (d) <= tolerance())
            current = target;
        else
            current += d * coeff;
        return (float) current;
    }

    // Advances n samples at once.
    float skip (int n)
    {
        const double d = target - current;
        if (std::abs (d) <= tolerance())
            current = target;
        else
            current = target - d * std::pow (1.0 - coeff, (double) n);
        return (float) current;
    }

private:
    double tolerance() const { return 1.0e-9 * (1.0 + std::abs (target)); }

    double current = 0.0, target = 0.0, coeff = 1.0;
};

//==============================================================================
// Power-of-two circular delay line with fractional reads. read*(d) returns the sample written d
// samples before the next write, so a feedback loop reads first, then writes.
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        int size = 16;
        while (size < maxDelaySamples + 8)
            size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    float maxDelay() const { return (float) (mask - 4); }

    void write (float x)
    {
        buffer[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    float at (int delay) const { return buffer[(size_t) ((writePos - delay) & mask)]; }

    float readLinear (float delay) const
    {
        delay = juce::jlimit (1.0f, maxDelay(), delay);
        const int i = (int) delay;
        const float f = delay - (float) i;
        const float a = at (i);
        return a + f * (at (i + 1) - a);
    }

    // 4-point Hermite interpolation; delays below 2 samples are clamped.
    float readCubic (float delay) const
    {
        delay = juce::jlimit (2.0f, maxDelay(), delay);
        const int i = (int) delay;
        const float f = delay - (float) i;
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

private:
    std::vector<float> buffer = std::vector<float> (16, 0.0f);
    int mask = 15, writePos = 0;
};

//==============================================================================
// EQ filters: trapezoidal state-variable filter with mixed outputs (A. Simper, "Linear
// trapezoidal integrated SVF"). Same magnitude responses as the RBJ cookbook shelves/bells, but
// well behaved when coefficients move every few samples (no transients on knob turns).
struct EqCoeffs
{
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f; // integrator coefficients
    float m0 = 1.0f, m1 = 0.0f, m2 = 0.0f; // output mix of input, band and low

    // Per-sample increment that moves `from` to `to` in n samples (coefficient ramps).
    static EqCoeffs delta (const EqCoeffs& from, const EqCoeffs& to, int n)
    {
        const float s = 1.0f / (float) juce::jmax (1, n);
        EqCoeffs d;
        d.a1 = (to.a1 - from.a1) * s; d.a2 = (to.a2 - from.a2) * s; d.a3 = (to.a3 - from.a3) * s;
        d.m0 = (to.m0 - from.m0) * s; d.m1 = (to.m1 - from.m1) * s; d.m2 = (to.m2 - from.m2) * s;
        return d;
    }

    void advance (const EqCoeffs& d)
    {
        a1 += d.a1; a2 += d.a2; a3 += d.a3;
        m0 += d.m0; m1 += d.m1; m2 += d.m2;
    }

    static EqCoeffs make (double sr, double f, double k, double gScale)
    {
        const double g = std::tan (juce::MathConstants<double>::pi * juce::jlimit (5.0, sr * 0.49, f) / sr) * gScale;
        const double a1 = 1.0 / (1.0 + g * (g + k));
        EqCoeffs c;
        c.a1 = (float) a1;
        c.a2 = (float) (g * a1);
        c.a3 = (float) (g * g * a1);
        return c;
    }

    static EqCoeffs lowShelf (double sr, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0), k = 1.0 / q;
        auto c = make (sr, f, k, 1.0 / std::sqrt (A));
        c.m0 = 1.0f;
        c.m1 = (float) (k * (A - 1.0));
        c.m2 = (float) (A * A - 1.0);
        return c;
    }

    static EqCoeffs highShelf (double sr, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0), k = 1.0 / q;
        auto c = make (sr, f, k, std::sqrt (A));
        c.m0 = (float) (A * A);
        c.m1 = (float) (k * (1.0 - A) * A);
        c.m2 = (float) (1.0 - A * A);
        return c;
    }

    static EqCoeffs peak (double sr, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0), k = 1.0 / (q * A);
        auto c = make (sr, f, k, 1.0);
        c.m0 = 1.0f;
        c.m1 = (float) (k * (A * A - 1.0));
        c.m2 = 0.0f;
        return c;
    }

    static EqCoeffs highPass (double sr, double f, double q)
    {
        const double k = 1.0 / q;
        auto c = make (sr, f, k, 1.0);
        c.m0 = 1.0f;
        c.m1 = (float) -k;
        c.m2 = -1.0f;
        return c;
    }
};

struct EqFilter
{
    float ic1 = 0.0f, ic2 = 0.0f;

    void reset() { ic1 = ic2 = 0.0f; }

    float process (const EqCoeffs& c, float x)
    {
        const float v3 = x - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return c.m0 * x + c.m1 * v1 + c.m2 * v2;
    }
};

//==============================================================================
// Switches between discrete modes (filter types, effect modes) without clicks: the wet signal
// fades to dry, the mode changes (the caller resets its state) and the wet signal fades back.
class ModeFade
{
public:
    void prepare (double sampleRate, double fadeSeconds = 0.008)
    {
        step = (float) (1.0 / (sampleRate * fadeSeconds));
    }

    void snap (int m) { current = target = m; gain = 1.0f; }
    void request (int m) { target = m; }
    int mode() const { return current; }
    bool isFading() const { return current != target || gain < 1.0f; }

    // Gain for this sample (0 = dry, 1 = wet), S-shaped. Returns true in `switched` when the
    // mode changed.
    float next (bool& switched)
    {
        switched = false;
        if (current != target)
        {
            gain -= step;
            if (gain <= 0.0f)
            {
                gain = 0.0f;
                current = target;
                switched = true;
            }
        }
        else if (gain < 1.0f)
        {
            gain = juce::jmin (1.0f, gain + step);
        }
        return gain * gain * (3.0f - 2.0f * gain);
    }

private:
    int current = 0, target = 0;
    float gain = 1.0f, step = 0.01f;
};

//==============================================================================
// Small, fast noise source (xorshift32), output in [-1, 1).
class Noise
{
public:
    explicit Noise (uint32_t seed = 0x9e3779b9u) : state (seed | 1u) {}
    void reseed (uint32_t seed) { state = seed | 1u; }

    float next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) (state >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }

private:
    uint32_t state;
};

// Value of a basic LFO shape at a phase in [0, 1): 0 sine, 1 triangle, 2 saw up, 3 saw down,
// 4 square. Output -1..1.
inline float lfoShape (int shape, float phase)
{
    switch (shape)
    {
        case 0:  return std::sin (twoPi * phase);
        case 1:  return 1.0f - 4.0f * std::abs (phase - 0.5f);
        case 2:  return 2.0f * phase - 1.0f;
        case 3:  return 1.0f - 2.0f * phase;
        default: return phase < 0.5f ? 1.0f : -1.0f;
    }
}

inline float wrapPhase (float p) { return p - std::floor (p); }

//==============================================================================
// Tempo divisions. Delay divisions are note lengths; LFO divisions are cycle lengths. The name
// lists are only built for specs (message thread); the audio thread uses the constexpr tables.
constexpr int numDelayDivisions = 14;
constexpr const char* delayDivisionLabels[numDelayDivisions] = { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D",
                                                                 "1/4T", "1/4", "1/4D", "1/2T", "1/2", "1/2D", "1/1" };
constexpr double delayDivisionBeatTable[numDelayDivisions] = { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75,
                                                               2.0 / 3.0, 1.0, 1.5, 4.0 / 3.0, 2.0, 3.0, 4.0 };

constexpr int numLfoDivisions = 12;
constexpr const char* lfoDivisionLabels[numLfoDivisions] = { "4 Bars", "2 Bars", "1/1", "1/2", "1/2T", "1/4",
                                                             "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
constexpr double lfoDivisionBeatTable[numLfoDivisions] = { 16.0, 8.0, 4.0, 2.0, 4.0 / 3.0, 1.0,
                                                           2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };

inline juce::StringArray delayDivisionNames() { return juce::StringArray (delayDivisionLabels, numDelayDivisions); }
inline juce::StringArray lfoDivisionNames()   { return juce::StringArray (lfoDivisionLabels, numLfoDivisions); }

inline double delayDivisionBeats (int index) { return delayDivisionBeatTable[juce::jlimit (0, numDelayDivisions - 1, index)]; }
inline double lfoDivisionBeats (int index)   { return lfoDivisionBeatTable[juce::jlimit (0, numLfoDivisions - 1, index)]; }

inline double clampTempo (double bpm) { return std::isfinite (bpm) ? juce::jlimit (20.0, 999.0, bpm) : 120.0; }

// Mix helper: dry + mix * (wet - dry), exact dry at mix 0.
inline float mixDryWet (float dry, float wet, float mix) { return dry + mix * (wet - dry); }

} // namespace rb::fx
