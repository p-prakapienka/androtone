#pragma once

// Small DSP building blocks shared by the instruments and effects. Everything here is
// allocation-free after construction and safe to use on the audio thread.

namespace rb::dsp
{

constexpr float twoPi = juce::MathConstants<float>::twoPi;

inline float midiToHz (float note) { return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f); }
inline float semitonesToRatio (float st) { return std::pow (2.0f, st / 12.0f); }
inline float dbToGain (float db) { return db <= -99.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }

// One-pole smoother for parameters, ~5 ms by default.
class Smoother
{
public:
    void prepare (double sampleRate, float timeSeconds = 0.005f)
    {
        coeff = 1.0f - std::exp (-1.0f / (float) (sampleRate * timeSeconds));
    }
    void setTarget (float t) { target = t; }
    void snap (float v) { current = target = v; }
    float next() { current += (target - current) * coeff; return current; }
    float getTarget() const { return target; }
    float getCurrent() const { return current; }

private:
    float current = 0.0f, target = 0.0f, coeff = 1.0f;
};

// Topology-preserving-transform state variable filter (Zavalishin). Stable under fast
// modulation, which the synths and Auto Filter rely on.
class Svf
{
public:
    enum class Mode { lowpass, bandpass, highpass, notch, peak, allpass };

    void prepare (double sr) { sampleRate = (float) sr; reset(); }
    void reset() { ic1 = ic2 = 0.0f; }

    // resonance 0..1 (self-oscillation is avoided near 1)
    void set (float cutoffHz, float resonance)
    {
        cutoffHz = juce::jlimit (10.0f, sampleRate * 0.49f, cutoffHz);
        g = std::tan (juce::MathConstants<float>::pi * cutoffHz / sampleRate);
        k = 2.0f - 1.98f * juce::jlimit (0.0f, 1.0f, resonance);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    struct Outputs { float low, band, high; };

    Outputs tick (float x)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return { v2, v1, x - k * v1 - v2 };
    }

    float process (float x, Mode mode)
    {
        const auto o = tick (x);
        switch (mode)
        {
            case Mode::lowpass:  return o.low;
            case Mode::bandpass: return o.band;
            case Mode::highpass: return o.high;
            case Mode::notch:    return o.low + o.high;
            case Mode::peak:     return o.low - o.high;
            case Mode::allpass:  return o.low + o.high - k * o.band;
        }
        return o.low;
    }

    float getK() const { return k; }

private:
    float sampleRate = 48000.0f;
    float g = 0.0f, k = 2.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
};

// Linear-segment ADSR with exponential-feeling release, times in seconds.
class Envelope
{
public:
    enum class Stage { idle, attack, decay, sustain, release };

    void prepare (double sr) { sampleRate = (float) sr; }
    void set (float attackS, float decayS, float sustainLevel, float releaseS)
    {
        attack = juce::jmax (0.0005f, attackS);
        decay = juce::jmax (0.001f, decayS);
        sustain = juce::jlimit (0.0f, 1.0f, sustainLevel);
        release = juce::jmax (0.001f, releaseS);
    }

    void noteOn()  { stage = Stage::attack; }
    void noteOff() { if (stage != Stage::idle) stage = Stage::release; }
    void reset()   { stage = Stage::idle; level = 0.0f; }
    bool isActive() const { return stage != Stage::idle; }
    Stage getStage() const { return stage; }
    float getLevel() const { return level; }

    float next()
    {
        switch (stage)
        {
            case Stage::idle: return 0.0f;
            case Stage::attack:
                level += 1.0f / (attack * sampleRate);
                if (level >= 1.0f) { level = 1.0f; stage = Stage::decay; }
                break;
            case Stage::decay:
                level -= (1.0f - sustain) / (decay * sampleRate) + 1.0e-7f;
                if (level <= sustain) { level = sustain; stage = sustain > 0.0f ? Stage::sustain : Stage::idle; }
                break;
            case Stage::sustain: level = sustain; break;
            case Stage::release:
                level -= level * (6.9f / (release * sampleRate)) + 1.0e-6f;
                if (level <= 1.0e-4f) { level = 0.0f; stage = Stage::idle; }
                break;
        }
        return level;
    }

private:
    float sampleRate = 48000.0f;
    float attack = 0.01f, decay = 0.1f, sustain = 1.0f, release = 0.1f;
    float level = 0.0f;
    Stage stage = Stage::idle;
};

// Band-limited oscillator (PolyBLEP) with continuous shape: 0 = sine, 1 = triangle,
// 2 = saw, 3 = square/pulse. pulseWidth applies to the square.
class Oscillator
{
public:
    enum Shape { sine = 0, triangle, saw, square };

    void prepare (double sr) { sampleRate = (float) sr; }
    void setFrequency (float hz) { increment = juce::jlimit (0.0f, 0.49f, hz / sampleRate); }
    void resetPhase (float p = 0.0f) { phase = p; triState = 0.0f; }
    float getPhase() const { return phase; }

    float next (int shape, float pulseWidth = 0.5f)
    {
        float out = 0.0f;
        const float dt = increment;

        switch (shape)
        {
            case sine:
                out = std::sin (twoPi * phase);
                break;
            case saw:
                out = 2.0f * phase - 1.0f - polyBlep (phase, dt);
                break;
            case square:
            {
                const float pw = juce::jlimit (0.05f, 0.95f, pulseWidth);
                out = phase < pw ? 1.0f : -1.0f;
                out += polyBlep (phase, dt);
                out -= polyBlep (std::fmod (phase + 1.0f - pw, 1.0f), dt);
                break;
            }
            case triangle:
            default:
            {
                // Integrated band-limited square.
                float sq = phase < 0.5f ? 1.0f : -1.0f;
                sq += polyBlep (phase, dt);
                sq -= polyBlep (std::fmod (phase + 0.5f, 1.0f), dt);
                triState = dt * sq * 4.0f + (1.0f - 0.0005f) * triState;
                out = triState;
                break;
            }
        }

        phase += dt;
        if (phase >= 1.0f)
            phase -= 1.0f;
        return out;
    }

private:
    static float polyBlep (float t, float dt)
    {
        if (dt <= 0.0f) return 0.0f;
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    float sampleRate = 48000.0f;
    float phase = 0.0f, increment = 0.0f, triState = 0.0f;
};

// Simple LFO with sine/triangle/saw/square/sample&hold, output -1..1.
class Lfo
{
public:
    enum Shape { sine = 0, triangle, sawUp, sawDown, square, sampleAndHold };

    void prepare (double sr) { sampleRate = (float) sr; }
    void setRate (float hz) { increment = hz / sampleRate; }
    void reset (float p = 0.0f) { phase = p; }

    float next (int shape)
    {
        float out = 0.0f;
        switch (shape)
        {
            case sine:     out = std::sin (twoPi * phase); break;
            case triangle: out = 1.0f - 4.0f * std::abs (phase - 0.5f); break;
            case sawUp:    out = 2.0f * phase - 1.0f; break;
            case sawDown:  out = 1.0f - 2.0f * phase; break;
            case square:   out = phase < 0.5f ? 1.0f : -1.0f; break;
            case sampleAndHold:
            default:       out = held; break;
        }
        phase += increment;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            held = random.nextFloat() * 2.0f - 1.0f;
        }
        return out;
    }

private:
    float sampleRate = 48000.0f, phase = 0.0f, increment = 0.0f, held = 0.0f;
    juce::Random random { 12345 };
};

// Splits a block at note-event boundaries: calls onEvent(event) at each event's offset and
// render(start, count) for the sample ranges between them.
template <typename OnEvent, typename Render>
void renderWithEvents (const NoteEvent* events, int numEvents, int numSamples, OnEvent&& onEvent, Render&& render)
{
    int position = 0;
    for (int i = 0; i < numEvents; ++i)
    {
        const int offset = juce::jlimit (0, numSamples, events[i].sampleOffset);
        if (offset > position)
        {
            render (position, offset - position);
            position = offset;
        }
        onEvent (events[i]);
    }
    if (position < numSamples)
        render (position, numSamples - position);
}

// Equal-power pan, pan in -1..1.
inline void panGains (float pan, float& left, float& right)
{
    const float angle = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    left = std::cos (angle) * juce::MathConstants<float>::sqrt2;
    right = std::sin (angle) * juce::MathConstants<float>::sqrt2;
}

} // namespace rb::dsp
