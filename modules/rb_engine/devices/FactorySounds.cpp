namespace rb::FactorySounds
{

// Every factory sound is synthesised here: drum hits from swept sines, filtered noise and
// square-wave clusters; tonal sounds from FM, additive, Karplus-Strong and filtered
// oscillators. All renders are deterministic (fixed seeds, no global state), 48 kHz, at most
// three seconds, peak-normalised to -1 dBFS. Tonal sounds sit at C3 (MIDI 60, 261.63 Hz) so
// the Sampler plays them at their original pitch on its root key (SMP-7).

namespace
{

constexpr float rate = 48000.0f;
constexpr float c3Hz = 261.6256f;
constexpr float pi = juce::MathConstants<float>::pi;
constexpr float twoPi = juce::MathConstants<float>::twoPi;
constexpr float maxSeconds = 3.0f;

using Signal = std::vector<float>;
using Channels = std::vector<Signal>;

size_t numFrames (float seconds)
{
    return (size_t) std::lround (juce::jlimit (0.001f, maxSeconds, seconds) * rate);
}

float timeAt (size_t i) { return (float) i / rate; }

// xorshift32: tiny, fast and identical on every platform.
struct Rng
{
    explicit Rng (juce::uint32 seed) : state (seed != 0 ? seed : 0x9E3779B9u) {}

    juce::uint32 nextInt()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    float unipolar() { return (float) (nextInt() >> 8) * (1.0f / 16777216.0f); }
    float bipolar()  { return unipolar() * 2.0f - 1.0f; }
    float range (float lo, float hi) { return lo + (hi - lo) * unipolar(); }

    juce::uint32 state;
};

//==============================================================================
// Generators

template <typename Fn>
Signal generate (float seconds, Fn&& fn)
{
    Signal x (numFrames (seconds));
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = fn (timeAt (i));
    return x;
}

Signal noise (float seconds, juce::uint32 seed)
{
    Rng rng (seed);
    return generate (seconds, [&rng] (float) { return rng.bipolar(); });
}

// Sine whose frequency glides exponentially: f(t) = end + (start - end) * exp(-t / tau).
Signal glideSine (float seconds, float startHz, float endHz, float tau, float phase = 0.0f)
{
    return generate (seconds, [=] (float t) mutable
    {
        const float f = endHz + (startHz - endHz) * std::exp (-t / tau);
        const float out = std::sin (phase);
        phase += twoPi * f / rate;
        if (phase > twoPi)
            phase -= twoPi;
        return out;
    });
}

// Band-limited oscillator (PolyBLEP) at a frequency given per sample.
template <typename FreqFn>
Signal oscillator (float seconds, int shape, FreqFn&& freqAt, float pulseWidth = 0.5f, float startPhase = 0.0f)
{
    dsp::Oscillator osc;
    osc.prepare (rate);
    osc.resetPhase (startPhase);
    return generate (seconds, [&] (float t)
    {
        osc.setFrequency (freqAt (t));
        return osc.next (shape, pulseWidth);
    });
}

Signal sine (float seconds, float hz, float phase = 0.0f)
{
    return glideSine (seconds, hz, hz, 1.0f, phase);
}

// Sum of square waves at inharmonic ratios: the classic analog cymbal/hat source.
Signal metallic (float seconds, float scale, juce::uint32 seed)
{
    static constexpr std::array<float, 6> ratios { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
    Rng rng (seed);
    Signal sum (numFrames (seconds), 0.0f);
    for (auto r : ratios)
    {
        const float hz = r * scale;
        auto osc = oscillator (seconds, dsp::Oscillator::square, [hz] (float) { return hz; }, 0.5f, rng.unipolar());
        for (size_t i = 0; i < sum.size(); ++i)
            sum[i] += osc[i] * (1.0f / 6.0f);
    }
    return sum;
}

//==============================================================================
// Processors

template <typename Fn>
void shape (Signal& x, Fn&& envelope)
{
    for (size_t i = 0; i < x.size(); ++i)
        x[i] *= envelope (timeAt (i));
}

void scale (Signal& x, float gain)
{
    for (auto& s : x)
        s *= gain;
}

void mixInto (Signal& dest, const Signal& source, float gain = 1.0f, float delaySeconds = 0.0f)
{
    const auto offset = (size_t) std::lround (delaySeconds * rate);
    for (size_t i = 0; i + offset < dest.size() && i < source.size(); ++i)
        dest[i + offset] += source[i] * gain;
}

template <typename CutoffFn>
void filterSweep (Signal& x, dsp::Svf::Mode mode, CutoffFn&& cutoffAt, float resonance)
{
    dsp::Svf f;
    f.prepare (rate);
    for (size_t i = 0; i < x.size(); ++i)
    {
        if ((i & 15) == 0)
            f.set (cutoffAt (timeAt (i)), resonance);
        x[i] = f.process (x[i], mode);
    }
}

void filter (Signal& x, dsp::Svf::Mode mode, float cutoff, float resonance = 0.0f)
{
    filterSweep (x, mode, [cutoff] (float) { return cutoff; }, resonance);
}

void lowpass (Signal& x, float cutoff, float resonance = 0.0f)  { filter (x, dsp::Svf::Mode::lowpass, cutoff, resonance); }
void highpass (Signal& x, float cutoff, float resonance = 0.0f) { filter (x, dsp::Svf::Mode::highpass, cutoff, resonance); }
void bandpass (Signal& x, float cutoff, float resonance = 0.0f) { filter (x, dsp::Svf::Mode::bandpass, cutoff, resonance); }

void drive (Signal& x, float amount)
{
    const float norm = 1.0f / std::tanh (amount);
    for (auto& s : x)
        s = std::tanh (s * amount) * norm;
}

// Bit depth and sample-rate reduction (sample and hold).
void crush (Signal& x, float bits, int hold)
{
    const float step = std::exp2 (1.0f - bits);
    float held = 0.0f;
    for (size_t i = 0; i < x.size(); ++i)
    {
        if (i % (size_t) juce::jmax (1, hold) == 0)
            held = std::round (x[i] / step) * step;
        x[i] = held;
    }
}

float peakOf (const Signal& x)
{
    float peak = 0.0f;
    for (auto s : x)
        peak = juce::jmax (peak, std::abs (s));
    return peak;
}

void normalise (Signal& x, float peak = 1.0f)
{
    const float p = peakOf (x);
    if (p > 0.0f)
        scale (x, peak / p);
}

float attackEnv (float t, float attack) { return attack <= 0.0f ? 1.0f : juce::jmin (1.0f, t / attack); }

// Smooth attack, exponential decay.
float hit (float t, float attack, float tau) { return attackEnv (t, attack) * std::exp (-t / tau); }

// Attack, sustain at 1, linear release over the last `release` seconds of `length`.
float sustained (float t, float attack, float length, float release)
{
    const float a = attack > 0.0f ? juce::jmin (1.0f, t / attack) : 1.0f;
    const float r = juce::jlimit (0.0f, 1.0f, (length - t) / release);
    return a * a * (3.0f - 2.0f * a) * r;
}

//==============================================================================
// Physical-ish models

// Karplus-Strong plucked string tuned to hz: delay line with a two-point averaging loop
// filter (half a sample of delay) and linear fractional delay.
Signal pluckString (float seconds, float hz, float feedback, float brightnessHz, float pickPosition, juce::uint32 seed)
{
    const float period = rate / hz;
    const float delay = period - 0.5f;
    const auto burst = (size_t) std::ceil (period);

    // Excitation: one period of filtered noise, comb-filtered for the pick position.
    Signal excitation = noise ((float) burst / rate + 0.001f, seed);
    lowpass (excitation, brightnessHz);
    if (pickPosition > 0.0f)
    {
        const auto offset = (size_t) std::lround (pickPosition * period);
        Signal combed = excitation;
        for (size_t i = offset; i < combed.size(); ++i)
            combed[i] -= excitation[i - offset];
        excitation = combed;
    }

    constexpr size_t lineSize = 4096;
    constexpr size_t mask = lineSize - 1;
    std::vector<float> line (lineSize, 0.0f);
    const auto whole = (size_t) delay;
    const float frac = delay - (float) whole;

    Signal out (numFrames (seconds));
    for (size_t i = 0; i < out.size(); ++i)
    {
        auto tap = [&] (size_t d)
        {
            const float a = line[(i + lineSize - d) & mask];
            const float b = line[(i + lineSize - d - 1) & mask];
            return a + (b - a) * frac;
        };
        const float r0 = tap (whole);
        const float r1 = tap (whole + 1);
        const float in = i < burst && i < excitation.size() ? excitation[i] : 0.0f;
        const float y = in + feedback * 0.5f * (r0 + r1);
        line[i & mask] = y;
        out[i] = y;
    }
    return out;
}

struct Partial
{
    float ratio, level, tau;
};

// Additive tone from decaying partials.
Signal additive (float seconds, float hz, std::initializer_list<Partial> partials, float attack, juce::uint32 seed)
{
    Rng rng (seed);
    Signal sum (numFrames (seconds), 0.0f);
    for (auto& p : partials)
    {
        const float f = hz * p.ratio;
        if (f >= rate * 0.45f)
            continue;
        const float phase = rng.unipolar() * twoPi;
        for (size_t i = 0; i < sum.size(); ++i)
        {
            const float t = timeAt (i);
            sum[i] += p.level * std::sin (twoPi * f * t + phase) * std::exp (-t / p.tau);
        }
    }
    shape (sum, [attack] (float t) { return attackEnv (t, attack); });
    return sum;
}

// Detuned band-limited saws (cents offsets), each with its own slow vibrato.
Signal sawEnsemble (float seconds, float hz, std::initializer_list<float> detuneCents, float vibratoDepth, juce::uint32 seed)
{
    Rng rng (seed);
    Signal sum (numFrames (seconds), 0.0f);
    const float gain = 1.0f / std::sqrt ((float) detuneCents.size());
    for (auto cents : detuneCents)
    {
        const float f = hz * std::exp2 (cents / 1200.0f);
        const float vibRate = rng.range (4.2f, 6.1f);
        const float vibPhase = rng.unipolar() * twoPi;
        auto saw = oscillator (seconds, dsp::Oscillator::saw, [=] (float t)
        {
            return f * (1.0f + vibratoDepth * std::sin (twoPi * vibRate * t + vibPhase));
        }, 0.5f, rng.unipolar());
        mixInto (sum, saw, gain);
    }
    return sum;
}

//==============================================================================
// Drums

struct KickSpec
{
    float length, startHz, endHz, pitchTau, hold, ampTau, click, driveAmount;
};

Signal kick (const KickSpec& k, juce::uint32 seed)
{
    auto body = glideSine (k.length, k.startHz, k.endHz, k.pitchTau);
    shape (body, [&k] (float t)
    {
        return attackEnv (t, 0.0012f) * (t < k.hold ? 1.0f : std::exp (-(t - k.hold) / k.ampTau));
    });

    if (k.click > 0.0f)
    {
        auto click = noise (0.02f, seed);
        bandpass (click, 3200.0f, 0.2f);
        highpass (click, 900.0f);
        shape (click, [] (float t) { return std::exp (-t / 0.0025f); });
        normalise (click);
        mixInto (body, click, k.click);
    }

    if (k.driveAmount > 0.0f)
        drive (body, k.driveAmount);
    return body;
}

Channels kickAcoustic()
{
    auto body = glideSine (0.8f, 115.0f, 60.0f, 0.018f);
    shape (body, [] (float t) { return hit (t, 0.001f, 0.12f); });

    auto shell = glideSine (0.8f, 190.0f, 165.0f, 0.02f, 1.0f);
    shape (shell, [] (float t) { return hit (t, 0.001f, 0.045f); });
    mixInto (body, shell, 0.3f);

    auto beater = noise (0.05f, 11);
    bandpass (beater, 2600.0f, 0.35f);
    shape (beater, [] (float t) { return std::exp (-t / 0.003f); });
    normalise (beater);
    mixInto (body, beater, 0.35f);

    auto thump = noise (0.15f, 12);
    lowpass (thump, 180.0f, 0.2f);
    shape (thump, [] (float t) { return hit (t, 0.001f, 0.03f); });
    normalise (thump);
    mixInto (body, thump, 0.3f);

    drive (body, 1.3f);
    return { body };
}

struct SnareSpec
{
    float length;
    float tone1, tone2, toneTau, toneLevel, sweep;
    float noiseHp, noiseLp, noiseTau, noiseLevel;
    float driveAmount;
};

Signal snare (const SnareSpec& s, juce::uint32 seed)
{
    auto body = glideSine (s.length, s.tone1 * (1.0f + s.sweep), s.tone1, 0.012f);
    auto overtone = glideSine (s.length, s.tone2 * (1.0f + s.sweep), s.tone2, 0.012f, 0.7f);
    mixInto (body, overtone, 0.6f);
    shape (body, [&s] (float t) { return hit (t, 0.0008f, s.toneTau); });

    auto wires = noise (s.length, seed);
    highpass (wires, s.noiseHp, 0.1f);
    lowpass (wires, s.noiseLp, 0.1f);
    lowpass (wires, s.noiseLp * 1.4f);
    shape (wires, [&s] (float t) { return hit (t, 0.0006f, s.noiseTau); });

    normalise (body);
    normalise (wires);
    Signal out (body.size(), 0.0f);
    mixInto (out, body, s.toneLevel);
    mixInto (out, wires, s.noiseLevel);
    if (s.driveAmount > 0.0f)
        drive (out, s.driveAmount);
    return out;
}

Signal clap (float length, float bandHz, int bursts, float spacing, float tailTau, juce::uint32 seed)
{
    auto x = noise (length, seed);
    bandpass (x, bandHz, 0.55f);
    highpass (x, 500.0f);
    lowpass (x, bandHz * 5.0f);

    Rng rng (seed + 7);
    std::vector<float> starts;
    float when = 0.0f;
    for (int b = 0; b < bursts; ++b)
    {
        starts.push_back (when);
        when += spacing * rng.range (0.8f, 1.25f);
    }

    shape (x, [&starts, tailTau] (float t)
    {
        float env = 0.0f;
        for (size_t b = 0; b < starts.size(); ++b)
        {
            const float local = t - starts[b];
            if (local < 0.0f)
                continue;
            const bool last = b + 1 == starts.size();
            env = juce::jmax (env, attackEnv (local, 0.0004f) * std::exp (-local / (last ? tailTau : 0.0045f)));
        }
        return env;
    });
    return x;
}

Signal hat (float length, float scaleFactor, float bandHz, float highHz, float noiseMix, float attack, float tau, juce::uint32 seed)
{
    auto metal = metallic (length, scaleFactor, seed);
    bandpass (metal, bandHz, 0.4f);
    normalise (metal);

    auto air = noise (length, seed + 1);
    highpass (air, highHz);
    normalise (air);

    Signal x (metal.size(), 0.0f);
    mixInto (x, metal, 1.0f - noiseMix);
    mixInto (x, air, noiseMix);
    highpass (x, highHz, 0.1f);
    highpass (x, highHz * 0.8f);
    lowpass (x, 13000.0f);
    lowpass (x, 16000.0f);
    shape (x, [attack, tau] (float t) { return hit (t, attack, tau); });
    return x;
}

Signal tom (float length, float hz, float sweep, float tau, float partialLevel, float noiseLevel, float driveAmount, juce::uint32 seed)
{
    auto body = glideSine (length, hz * (1.0f + sweep), hz, 0.05f);
    shape (body, [tau] (float t) { return hit (t, 0.001f, tau); });

    if (partialLevel > 0.0f)
    {
        auto partial = glideSine (length, hz * 1.5f * (1.0f + sweep), hz * 1.5f, 0.05f, 0.5f);
        shape (partial, [tau] (float t) { return hit (t, 0.001f, tau * 0.45f); });
        mixInto (body, partial, partialLevel);
    }

    if (noiseLevel > 0.0f)
    {
        auto stick = noise (0.05f, seed);
        bandpass (stick, 1400.0f, 0.2f);
        shape (stick, [] (float t) { return std::exp (-t / 0.008f); });
        normalise (stick);
        mixInto (body, stick, noiseLevel);
    }

    drive (body, driveAmount);
    return body;
}

Channels crash (float length, float fastTau, float slowTau, float highHz, juce::uint32 seed)
{
    Channels out;
    for (juce::uint32 ch = 0; ch < 2; ++ch)
    {
        const float detune = ch == 0 ? 1.0f : 1.013f;
        Signal x (numFrames (length), 0.0f);
        for (float s : { 2.1f, 3.3f, 4.7f })
        {
            auto m = metallic (length, s * detune, seed + ch * 31 + (juce::uint32) (s * 10.0f));
            mixInto (x, m, 0.6f);
        }
        auto n = noise (length, seed + 100 + ch);
        highpass (n, 5000.0f);
        mixInto (x, n, 0.25f);

        auto bright = x;
        bandpass (bright, 7500.0f, 0.15f);
        highpass (x, highHz, 0.1f);
        mixInto (x, bright, 0.7f);
        highpass (x, highHz * 0.7f);
        lowpass (x, 14000.0f);

        shape (x, [fastTau, slowTau] (float t)
        {
            return attackEnv (t, 0.0015f) * (0.55f * std::exp (-t / fastTau) + 0.45f * std::exp (-t / slowTau));
        });
        out.push_back (std::move (x));
    }
    return out;
}

Signal conga (float length, float hz, float tau, float slap, juce::uint32 seed)
{
    auto body = glideSine (length, hz * 1.12f, hz, 0.01f);
    shape (body, [tau] (float t) { return hit (t, 0.0008f, tau); });

    auto overtone = glideSine (length, hz * 1.62f * 1.1f, hz * 1.62f, 0.01f, 0.3f);
    shape (overtone, [tau] (float t) { return hit (t, 0.0008f, tau * 0.3f); });
    mixInto (body, overtone, 0.25f);

    auto skin = noise (0.04f, seed);
    bandpass (skin, 2200.0f, 0.3f);
    shape (skin, [] (float t) { return std::exp (-t / 0.005f); });
    normalise (skin);
    mixInto (body, skin, slap);
    drive (body, 1.2f);
    return body;
}

//==============================================================================
// FX

Channels riser()
{
    constexpr float len = 3.0f;
    Channels out;
    for (juce::uint32 ch = 0; ch < 2; ++ch)
    {
        auto n = noise (len, 900 + ch);
        filterSweep (n, dsp::Svf::Mode::bandpass, [] (float t) { return 300.0f * std::pow (30.0f, t / len); }, 0.55f);
        normalise (n);

        auto saw = oscillator (len, dsp::Oscillator::saw, [ch] (float t)
        {
            return 110.0f * std::pow (8.0f, t / len) * (ch == 0 ? 0.997f : 1.003f);
        });
        filterSweep (saw, dsp::Svf::Mode::lowpass, [] (float t) { return 400.0f * std::pow (20.0f, t / len); }, 0.3f);
        normalise (saw);

        Signal x (n.size(), 0.0f);
        mixInto (x, n, 0.7f);
        mixInto (x, saw, 0.35f);
        shape (x, [] (float t) { const float p = t / len; return p * p; });
        out.push_back (std::move (x));
    }
    return out;
}

Channels noiseSweep()
{
    constexpr float len = 2.6f;
    Channels out;
    for (juce::uint32 ch = 0; ch < 2; ++ch)
    {
        auto n = noise (len, 950 + ch);
        filterSweep (n, dsp::Svf::Mode::bandpass, [] (float t) { return 9000.0f * std::pow (250.0f / 9000.0f, juce::jmin (1.0f, t / 2.2f)); }, 0.6f);
        shape (n, [] (float t) { return hit (t, 0.01f, 0.9f); });
        out.push_back (std::move (n));
    }
    return out;
}

Signal glitch()
{
    constexpr float len = 0.9f;
    Rng rng (4242);
    Signal out (numFrames (len), 0.0f);
    size_t pos = 0;
    Signal previous;
    while (pos < out.size())
    {
        const float segLen = rng.range (0.015f, 0.07f);
        const int kind = (int) (rng.unipolar() * 5.0f);
        Signal seg;
        if (kind == 0)
        {
            seg = sine (segLen, rng.range (300.0f, 3000.0f));
            crush (seg, 4.0f, 3);
        }
        else if (kind == 1)
        {
            seg = noise (segLen, rng.nextInt());
            highpass (seg, rng.range (1000.0f, 6000.0f));
        }
        else if (kind == 2)
        {
            const float hz = rng.range (60.0f, 400.0f);
            seg = oscillator (segLen, dsp::Oscillator::square, [hz] (float) { return hz; });
        }
        else if (kind == 3 && ! previous.empty())
        {
            seg = previous; // stutter the last slice
        }
        else
        {
            seg = Signal (numFrames (segLen), 0.0f);
        }

        const float level = rng.range (0.4f, 1.0f);
        const size_t fade = 48;
        for (size_t i = 0; i < seg.size() && pos + i < out.size(); ++i)
        {
            const float edge = juce::jmin (1.0f, (float) juce::jmin (i, seg.size() - 1 - i) / (float) fade);
            out[pos + i] = seg[i] * level * edge;
        }
        pos += seg.size();
        previous = seg;
    }
    crush (out, 8.0f, 2);
    return out;
}

Channels vinyl()
{
    constexpr float len = 3.0f;
    Channels out;
    for (juce::uint32 ch = 0; ch < 2; ++ch)
    {
        auto hiss = noise (len, 700 + ch);
        lowpass (hiss, 5000.0f);
        highpass (hiss, 400.0f);
        scale (hiss, 0.03f);

        Signal crackle (hiss.size(), 0.0f);
        Rng rng (800 + ch);
        Rng grain (850 + ch);
        for (size_t i = 0; i < crackle.size(); ++i)
        {
            if (rng.unipolar() < 30.0f / rate)
            {
                // A tiny decaying burst: one crackle.
                const float amp = rng.range (0.1f, 0.7f) * (rng.unipolar() < 0.08f ? 1.6f : 1.0f);
                const auto burst = (size_t) rng.range (8.0f, 40.0f);
                for (size_t k = 0; k < burst && i + k < crackle.size(); ++k)
                    crackle[i + k] += amp * grain.bipolar() * std::exp (-(float) k / ((float) burst * 0.3f));
            }
        }
        highpass (crackle, 700.0f);
        lowpass (crackle, 9000.0f);
        mixInto (hiss, crackle);

        // A faint low rumble that wobbles at record speed (33 1/3 rpm).
        auto rumble = noise (len, 760 + ch);
        lowpass (rumble, 60.0f);
        normalise (rumble, 0.02f);
        shape (rumble, [] (float t) { return 0.6f + 0.4f * std::sin (twoPi * 0.555f * t); });
        mixInto (hiss, rumble);

        shape (hiss, [] (float t) { return sustained (t, 0.05f, len, 0.15f); });
        out.push_back (std::move (hiss));
    }
    return out;
}

//==============================================================================
// Tonal (all at C3)

Channels electricPiano()
{
    constexpr float len = 3.0f;
    const float f = c3Hz;
    auto x = generate (len, [f] (float t)
    {
        const float body = std::sin (twoPi * f * t + (1.1f * std::exp (-t / 0.5f) + 0.12f) * std::sin (twoPi * f * t));
        const float tine = std::sin (twoPi * f * t + 1.6f * std::exp (-t / 0.04f) * std::sin (twoPi * 14.0f * f * t));
        const float chorus = std::sin (twoPi * f * 1.0025f * t + 0.5f * std::exp (-t / 0.3f) * std::sin (twoPi * f * t));
        return (0.6f * body * std::exp (-t / 1.6f) + 0.3f * tine * std::exp (-t / 0.9f) + 0.25f * chorus * std::exp (-t / 1.4f))
               * attackEnv (t, 0.002f);
    });
    shape (x, [] (float t) { return sustained (t, 0.0f, len, 0.3f); });
    return { x };
}

Channels pluck()
{
    auto x = pluckString (2.2f, c3Hz, 0.996f, 6000.0f, 0.12f, 301);
    lowpass (x, 7000.0f);
    return { x };
}

Channels nylon()
{
    auto x = pluckString (2.6f, c3Hz, 0.9975f, 2400.0f, 0.2f, 302);
    // A soft body resonance.
    auto body = x;
    bandpass (body, 420.0f, 0.4f);
    mixInto (x, body, 0.25f);
    lowpass (x, 3800.0f);
    return { x };
}

Channels bell()
{
    auto x = additive (3.0f, c3Hz,
                       { { 1.0f, 1.0f, 1.6f }, { 1.003f, 0.35f, 1.4f }, { 2.0f, 0.45f, 0.9f }, { 3.0f, 0.22f, 0.5f },
                         { 2.76f, 0.3f, 0.35f }, { 4.07f, 0.15f, 0.3f }, { 5.4f, 0.18f, 0.18f }, { 8.93f, 0.1f, 0.08f } },
                       0.0015f, 401);
    shape (x, [] (float t) { return sustained (t, 0.0f, 3.0f, 0.4f); });
    return { x };
}

Channels marimba()
{
    auto x = additive (1.6f, c3Hz, { { 1.0f, 1.0f, 0.45f }, { 3.93f, 0.3f, 0.07f }, { 9.8f, 0.08f, 0.02f } }, 0.001f, 402);
    auto mallet = noise (0.03f, 403);
    lowpass (mallet, 2500.0f);
    shape (mallet, [] (float t) { return std::exp (-t / 0.003f); });
    normalise (mallet);
    mixInto (x, mallet, 0.08f);
    return { x };
}

Channels vibes()
{
    auto x = additive (3.0f, c3Hz, { { 1.0f, 1.0f, 1.8f }, { 4.0f, 0.3f, 0.35f }, { 10.0f, 0.06f, 0.06f } }, 0.001f, 404);
    shape (x, [] (float t) { return (0.82f + 0.18f * std::cos (twoPi * 5.5f * t)) * sustained (t, 0.0f, 3.0f, 0.4f); });
    return { x };
}

Channels kalimba()
{
    auto x = generate (2.2f, [] (float t)
    {
        const float f = c3Hz * (1.0f + 0.004f * std::exp (-t / 0.02f));
        return std::sin (twoPi * f * t) * std::exp (-t / 0.8f)
             + 0.28f * std::sin (twoPi * c3Hz * 5.93f * t) * std::exp (-t / 0.06f)
             + 0.08f * std::sin (twoPi * c3Hz * 2.0f * t) * std::exp (-t / 0.3f);
    });
    auto thumb = noise (0.03f, 405);
    bandpass (thumb, 1800.0f, 0.3f);
    shape (thumb, [] (float t) { return std::exp (-t / 0.004f); });
    normalise (thumb);
    mixInto (x, thumb, 0.1f);
    shape (x, [] (float t) { return attackEnv (t, 0.001f); });
    return { x };
}

Channels softPad()
{
    constexpr float len = 3.0f;
    Channels out;
    for (juce::uint32 ch = 0; ch < 2; ++ch)
    {
        auto x = ch == 0 ? sawEnsemble (len, c3Hz, { -9.0f, 0.0f, 6.0f }, 0.0015f, 501)
                         : sawEnsemble (len, c3Hz, { -6.0f, 0.0f, 9.0f }, 0.0015f, 502);
        auto sub = sine (len, c3Hz);
        mixInto (x, sub, 0.5f);
        lowpass (x, 1400.0f, 0.15f);
        shape (x, [] (float t) { return sustained (t, 0.6f, len, 0.5f); });
        out.push_back (std::move (x));
    }
    return out;
}

Channels choir()
{
    constexpr float len = 3.0f;
    // "Ah" vowel formants (Hz, resonance, level)
    static constexpr std::array<std::array<float, 3>, 4> formants { { { 700.0f, 0.75f, 1.0f },
                                                                       { 1150.0f, 0.8f, 0.6f },
                                                                       { 2600.0f, 0.85f, 0.35f },
                                                                       { 3300.0f, 0.85f, 0.2f } } };
    Channels out;
    for (juce::uint32 ch = 0; ch < 2; ++ch)
    {
        auto source = ch == 0 ? sawEnsemble (len, c3Hz, { -11.0f, -4.0f, 3.0f, 10.0f }, 0.003f, 601)
                              : sawEnsemble (len, c3Hz, { -9.0f, -2.0f, 5.0f, 12.0f }, 0.003f, 602);
        Signal x (source.size(), 0.0f);
        for (auto& fm : formants)
        {
            auto band = source;
            bandpass (band, fm[0], fm[1]);
            mixInto (x, band, fm[2] * 0.4f);
        }
        auto chest = source;
        lowpass (chest, 500.0f);
        mixInto (x, chest, 0.5f);

        auto breath = noise (len, 610 + ch);
        bandpass (breath, 2800.0f, 0.4f);
        mixInto (x, breath, 0.015f);
        shape (x, [] (float t) { return sustained (t, 0.45f, len, 0.5f); });
        out.push_back (std::move (x));
    }
    return out;
}

Channels brass()
{
    constexpr float len = 1.8f;
    auto x = sawEnsemble (len, c3Hz, { -4.0f, 4.0f }, 0.0f, 701);
    // Pitch scoop into the note.
    auto scoop = oscillator (len, dsp::Oscillator::saw, [] (float t) { return c3Hz * (1.0f - 0.03f * std::exp (-t / 0.03f)); });
    mixInto (x, scoop, 0.6f);
    filterSweep (x, dsp::Svf::Mode::lowpass, [] (float t)
    {
        const float env = attackEnv (t, 0.035f) * (0.45f + 0.55f * std::exp (-juce::jmax (0.0f, t - 0.035f) / 0.15f));
        return 350.0f + 3800.0f * env;
    }, 0.15f);
    shape (x, [] (float t) { return sustained (t, 0.018f, len, 0.3f) * (0.85f + 0.15f * std::exp (-t / 0.1f)); });
    return { x };
}

Channels strings()
{
    constexpr float len = 3.0f;
    Channels out;
    for (juce::uint32 ch = 0; ch < 2; ++ch)
    {
        auto x = ch == 0 ? sawEnsemble (len, c3Hz, { -12.0f, -7.0f, -2.0f, 3.0f, 8.0f, 11.0f }, 0.0025f, 801)
                         : sawEnsemble (len, c3Hz, { -10.0f, -5.0f, 0.0f, 4.0f, 7.0f, 12.0f }, 0.0025f, 802);
        lowpass (x, 3200.0f, 0.1f);
        highpass (x, 160.0f);
        auto body = x;
        bandpass (body, 900.0f, 0.3f);
        mixInto (x, body, 0.3f);
        shape (x, [] (float t) { return sustained (t, 0.35f, len, 0.5f); });
        out.push_back (std::move (x));
    }
    return out;
}

Channels organ()
{
    constexpr float len = 2.8f;
    Channels out;
    for (juce::uint32 ch = 0; ch < 2; ++ch)
    {
        // Drawbars 8', 4', 2 2/3', 2' and a touch of 1'.
        const float rotorPhase = ch == 0 ? 0.0f : 1.6f;
        auto x = generate (len, [rotorPhase] (float t)
        {
            const float rotor = 1.0f + 0.12f * std::sin (twoPi * 6.2f * t + rotorPhase);
            const float vib = 0.0008f * std::sin (twoPi * 6.2f * t + rotorPhase);
            const float ph = twoPi * c3Hz * t * (1.0f + vib);
            return rotor * (std::sin (ph) + 0.7f * std::sin (2.0f * ph) + 0.5f * std::sin (3.0f * ph)
                            + 0.4f * std::sin (4.0f * ph) + 0.15f * std::sin (8.0f * ph));
        });
        auto click = noise (0.01f, 901 + ch);
        bandpass (click, 3000.0f, 0.2f);
        shape (click, [] (float t) { return std::exp (-t / 0.0015f); });
        normalise (click);
        mixInto (x, click, 0.25f);
        shape (x, [] (float t) { return sustained (t, 0.004f, len, 0.06f); });
        out.push_back (std::move (x));
    }
    return out;
}

Channels bassPluck()
{
    constexpr float len = 1.6f;
    auto saw = oscillator (len, dsp::Oscillator::saw, [] (float) { return c3Hz; });
    auto square = oscillator (len, dsp::Oscillator::square, [] (float) { return c3Hz; }, 0.5f, 0.25f);
    mixInto (saw, square, 0.5f);
    filterSweep (saw, dsp::Svf::Mode::lowpass, [] (float t) { return 300.0f + 3200.0f * std::exp (-t / 0.09f); }, 0.35f);
    shape (saw, [] (float t) { return hit (t, 0.002f, 0.55f) * sustained (t, 0.0f, len, 0.1f); });
    drive (saw, 1.5f);
    return { saw };
}

Channels lead()
{
    constexpr float len = 2.2f;
    auto vibrato = [] (float t)
    {
        const float depth = 0.006f * juce::jlimit (0.0f, 1.0f, (t - 0.25f) / 0.3f);
        return 1.0f + depth * std::sin (twoPi * 5.6f * t);
    };
    auto x = oscillator (len, dsp::Oscillator::saw, [vibrato] (float t) { return c3Hz * vibrato (t); });
    auto detuned = oscillator (len, dsp::Oscillator::saw, [vibrato] (float t) { return c3Hz * 1.004f * vibrato (t); }, 0.5f, 0.3f);
    auto pulse = oscillator (len, dsp::Oscillator::square, [vibrato] (float t) { return c3Hz * vibrato (t); }, 0.3f);
    mixInto (x, detuned, 0.7f);
    mixInto (x, pulse, 0.4f);
    lowpass (x, 4200.0f, 0.25f);
    shape (x, [] (float t) { return sustained (t, 0.006f, len, 0.25f); });
    return { x };
}

Channels flute()
{
    constexpr float len = 2.4f;
    auto x = generate (len, [] (float t)
    {
        const float depth = 0.004f * juce::jlimit (0.0f, 1.0f, (t - 0.3f) / 0.4f);
        const float ph = twoPi * c3Hz * t + depth * 40.0f * std::sin (twoPi * 5.0f * t);
        return std::sin (ph) + 0.18f * std::sin (2.0f * ph) + 0.06f * std::sin (3.0f * ph);
    });
    auto breath = noise (len, 1001);
    bandpass (breath, 1600.0f, 0.3f);
    normalise (breath);
    shape (breath, [] (float t) { return 0.05f + 0.15f * std::exp (-t / 0.08f); });
    mixInto (x, breath, 0.6f);
    shape (x, [] (float t) { return sustained (t, 0.09f, len, 0.3f); });
    return { x };
}

//==============================================================================

using Renderer = Channels (*)();

struct Entry
{
    const char* id;
    const char* name;
    const char* category;
    Renderer render;
};

Channels mono (Signal x) { Channels c; c.push_back (std::move (x)); return c; }

const std::vector<Entry>& entries()
{
    static const std::vector<Entry> table {
        // Kicks
        { "kick-boom", "Boom Kick", "Kick", [] { return mono (kick ({ 2.6f, 140.0f, 46.0f, 0.05f, 0.05f, 0.55f, 0.05f, 1.6f }, 101)); } },
        { "kick-punch", "Punch Kick", "Kick", [] { return mono (kick ({ 0.8f, 260.0f, 55.0f, 0.022f, 0.02f, 0.14f, 0.25f, 2.5f }, 102)); } },
        { "kick-acoustic", "Acoustic Kick", "Kick", [] { return kickAcoustic(); } },
        { "kick-sub", "Sub Kick", "Kick", [] { return mono (kick ({ 2.8f, 72.0f, 42.0f, 0.08f, 0.1f, 0.55f, 0.0f, 1.2f }, 104)); } },
        { "kick-lofi", "Lo-Fi Kick", "Kick", []
          {
              auto x = kick ({ 1.0f, 200.0f, 52.0f, 0.025f, 0.02f, 0.2f, 0.2f, 2.0f }, 105);
              crush (x, 7.0f, 3);
              lowpass (x, 6000.0f);
              return mono (x);
          } },
        { "kick-distorted", "Distorted Kick", "Kick", []
          {
              auto x = kick ({ 1.3f, 320.0f, 50.0f, 0.03f, 0.05f, 0.2f, 0.2f, 8.0f }, 106);
              lowpass (x, 7000.0f);
              return mono (x);
          } },
        { "kick-techno", "Techno Kick", "Kick", [] { return mono (kick ({ 0.6f, 190.0f, 48.0f, 0.018f, 0.01f, 0.11f, 0.15f, 2.2f }, 107)); } },
        { "kick-deep", "Deep Kick", "Kick", [] { return mono (kick ({ 1.5f, 120.0f, 48.0f, 0.04f, 0.03f, 0.3f, 0.05f, 1.5f }, 108)); } },
        { "kick-tight", "Tight Kick", "Kick", [] { return mono (kick ({ 0.4f, 230.0f, 65.0f, 0.012f, 0.0f, 0.06f, 0.3f, 1.8f }, 109)); } },

        // Snares
        { "snare-acoustic", "Acoustic Snare", "Snare", [] { return mono (snare ({ 0.9f, 185.0f, 330.0f, 0.07f, 0.8f, 0.3f, 1200.0f, 7000.0f, 0.16f, 0.7f, 1.5f }, 201)); } },
        { "snare-808", "Analog Snare", "Snare", [] { return mono (snare ({ 0.6f, 180.0f, 330.0f, 0.09f, 1.0f, 0.1f, 1800.0f, 12000.0f, 0.12f, 0.6f, 1.2f }, 202)); } },
        { "snare-tight", "Tight Snare", "Snare", [] { return mono (snare ({ 0.35f, 240.0f, 420.0f, 0.04f, 0.6f, 0.2f, 2500.0f, 11000.0f, 0.07f, 0.8f, 2.0f }, 203)); } },
        { "snare-fat", "Fat Snare", "Snare", [] { return mono (snare ({ 1.25f, 160.0f, 280.0f, 0.1f, 1.0f, 0.5f, 900.0f, 6000.0f, 0.25f, 0.8f, 3.0f }, 204)); } },
        { "snare-lofi", "Lo-Fi Snare", "Snare", []
          {
              auto x = snare ({ 0.6f, 190.0f, 340.0f, 0.06f, 0.8f, 0.3f, 1200.0f, 9000.0f, 0.14f, 0.8f, 2.0f }, 205);
              crush (x, 6.0f, 4);
              lowpass (x, 5000.0f);
              return mono (x);
          } },
        { "snare-brush", "Brush Snare", "Snare", []
          {
              auto x = noise (0.75f, 206);
              highpass (x, 800.0f);
              lowpass (x, 5000.0f);
              lowpass (x, 7000.0f);
              shape (x, [] (float t) { return hit (t, 0.008f, 0.15f); });
              return mono (x);
          } },
        { "snare-electro", "Electro Snare", "Snare", []
          {
              auto tone = glideSine (0.45f, 600.0f, 220.0f, 0.015f);
              shape (tone, [] (float t) { return hit (t, 0.0005f, 0.06f); });
              auto n = noise (0.45f, 207);
              highpass (n, 3000.0f);
              lowpass (n, 9000.0f);
              shape (n, [] (float t) { return hit (t, 0.0005f, 0.09f); });
              normalise (n);
              mixInto (tone, n, 0.7f);
              drive (tone, 2.5f);
              return mono (tone);
          } },
        { "snare-trap", "Trap Snare", "Snare", [] { return mono (snare ({ 0.5f, 280.0f, 520.0f, 0.05f, 0.5f, 0.2f, 3000.0f, 11000.0f, 0.1f, 1.0f, 1.5f }, 208)); } },

        // Claps
        { "clap-classic", "Classic Clap", "Clap", [] { return mono (clap (0.75f, 1100.0f, 4, 0.011f, 0.13f, 301)); } },
        { "clap-tight", "Tight Clap", "Clap", [] { return mono (clap (0.35f, 1700.0f, 3, 0.008f, 0.07f, 302)); } },
        { "clap-big", "Big Clap", "Clap", []
          {
              Channels c;
              c.push_back (clap (1.3f, 950.0f, 5, 0.013f, 0.25f, 303));
              c.push_back (clap (1.3f, 1000.0f, 5, 0.013f, 0.26f, 304));
              return c;
          } },
        { "clap-lofi", "Lo-Fi Clap", "Clap", []
          {
              auto x = clap (0.5f, 1200.0f, 4, 0.011f, 0.12f, 305);
              crush (x, 6.0f, 3);
              lowpass (x, 4000.0f);
              return mono (x);
          } },
        { "clap-single", "Single Clap", "Clap", [] { return mono (clap (0.3f, 1300.0f, 1, 0.01f, 0.035f, 306)); } },

        // Hats
        { "hat-closed", "Closed Hat", "Hat", [] { return mono (hat (0.3f, 1.6f, 10000.0f, 7000.0f, 0.3f, 0.0005f, 0.04f, 401)); } },
        { "hat-closed-tight", "Tight Closed Hat", "Hat", [] { return mono (hat (0.15f, 1.8f, 10500.0f, 7500.0f, 0.25f, 0.0003f, 0.018f, 402)); } },
        { "hat-open", "Open Hat", "Hat", [] { return mono (hat (1.7f, 1.6f, 10000.0f, 7000.0f, 0.3f, 0.001f, 0.35f, 403)); } },
        { "hat-open-short", "Short Open Hat", "Hat", [] { return mono (hat (0.7f, 1.7f, 10000.0f, 7000.0f, 0.3f, 0.001f, 0.14f, 404)); } },
        { "hat-pedal", "Pedal Hat", "Hat", [] { return mono (hat (0.3f, 1.4f, 8000.0f, 5500.0f, 0.35f, 0.004f, 0.05f, 405)); } },
        { "hat-noise", "Noise Hat", "Hat", [] { return mono (hat (0.3f, 1.6f, 10000.0f, 8000.0f, 1.0f, 0.0005f, 0.045f, 406)); } },
        { "hat-trap", "Trap Hat", "Hat", [] { return mono (hat (0.15f, 2.0f, 12000.0f, 8500.0f, 0.3f, 0.0003f, 0.022f, 407)); } },
        { "hat-lofi", "Lo-Fi Hat", "Hat", []
          {
              auto x = hat (0.25f, 1.6f, 9000.0f, 6500.0f, 0.3f, 0.0005f, 0.04f, 408);
              crush (x, 6.0f, 4);
              lowpass (x, 8000.0f);
              return mono (x);
          } },

        // Toms
        { "tom-low", "Low Tom", "Tom", [] { return mono (tom (1.4f, 85.0f, 0.5f, 0.28f, 0.3f, 0.15f, 1.3f, 501)); } },
        { "tom-mid", "Mid Tom", "Tom", [] { return mono (tom (1.1f, 125.0f, 0.45f, 0.22f, 0.3f, 0.15f, 1.3f, 502)); } },
        { "tom-high", "High Tom", "Tom", [] { return mono (tom (0.9f, 175.0f, 0.4f, 0.18f, 0.3f, 0.15f, 1.3f, 503)); } },
        { "tom-floor", "Floor Tom", "Tom", [] { return mono (tom (1.9f, 65.0f, 0.5f, 0.38f, 0.3f, 0.12f, 1.3f, 504)); } },
        { "tom-electro", "Electro Tom", "Tom", [] { return mono (tom (1.4f, 120.0f, 1.2f, 0.3f, 0.0f, 0.05f, 1.6f, 505)); } },

        // Percussion
        { "perc-rim", "Rimshot", "Perc", []
          {
              auto x = sine (0.12f, 1700.0f);
              shape (x, [] (float t) { return std::exp (-t / 0.006f); });
              auto low = sine (0.12f, 520.0f);
              shape (low, [] (float t) { return std::exp (-t / 0.012f); });
              mixInto (x, low, 0.6f);
              auto n = noise (0.05f, 601);
              bandpass (n, 3000.0f, 0.3f);
              shape (n, [] (float t) { return std::exp (-t / 0.004f); });
              normalise (n);
              mixInto (x, n, 0.5f);
              highpass (x, 300.0f);
              return mono (x);
          } },
        { "perc-cowbell", "Cowbell", "Perc", []
          {
              auto x = oscillator (0.6f, dsp::Oscillator::square, [] (float) { return 540.0f; });
              auto b = oscillator (0.6f, dsp::Oscillator::square, [] (float) { return 800.0f; }, 0.5f, 0.3f);
              mixInto (x, b);
              bandpass (x, 900.0f, 0.4f);
              highpass (x, 400.0f);
              shape (x, [] (float t) { return attackEnv (t, 0.0005f) * (0.6f * std::exp (-t / 0.015f) + 0.4f * std::exp (-t / 0.13f)); });
              return mono (x);
          } },
        { "perc-clave", "Clave", "Perc", []
          {
              auto x = sine (0.2f, 2500.0f);
              auto h = sine (0.2f, 5100.0f);
              shape (h, [] (float t) { return std::exp (-t / 0.006f); });
              mixInto (x, h, 0.15f);
              shape (x, [] (float t) { return hit (t, 0.0003f, 0.028f); });
              return mono (x);
          } },
        { "perc-shaker", "Shaker", "Perc", []
          {
              auto x = noise (0.25f, 602);
              highpass (x, 4500.0f, 0.2f);
              lowpass (x, 12000.0f);
              Rng rng (603);
              Signal grains (x.size());
              for (auto& g : grains)
                  g = rng.unipolar() < 0.3f ? 1.0f : 0.45f;
              for (size_t i = 0; i < x.size(); ++i)
                  x[i] *= grains[i];
              shape (x, [] (float t)
              {
                  const float rise = juce::jmin (1.0f, t / 0.018f);
                  return rise * rise * std::exp (-juce::jmax (0.0f, t - 0.018f) / 0.045f);
              });
              return mono (x);
          } },
        { "perc-tambourine", "Tambourine", "Perc", []
          {
              Rng rng (604);
              Signal jingles (numFrames (1.1f), 0.0f);
              for (int j = 0; j < 10; ++j)
              {
                  auto s = sine (1.1f, rng.range (4500.0f, 11000.0f), rng.unipolar() * twoPi);
                  const float tau = rng.range (0.12f, 0.25f);
                  shape (s, [tau] (float t) { return std::exp (-t / tau); });
                  mixInto (jingles, s, 0.15f);
              }
              auto n = noise (1.1f, 605);
              highpass (n, 6000.0f);
              shape (n, [] (float t) { return std::exp (-t / 0.1f); });
              normalise (n, 0.5f);
              mixInto (jingles, n);
              // Jingle chatter: three quick strikes.
              shape (jingles, [] (float t)
              {
                  float env = 0.0f;
                  for (float start : { 0.0f, 0.007f, 0.016f })
                      if (t >= start)
                          env += attackEnv (t - start, 0.0005f) * (start > 0.0f ? 0.5f : 1.0f);
                  return env;
              });
              auto skin = sine (1.1f, 210.0f);
              shape (skin, [] (float t) { return hit (t, 0.001f, 0.03f); });
              mixInto (jingles, skin, 0.3f);
              return mono (jingles);
          } },
        { "perc-conga-high", "High Conga", "Perc", [] { return mono (conga (0.8f, 330.0f, 0.16f, 0.35f, 606)); } },
        { "perc-conga-low", "Low Conga", "Perc", [] { return mono (conga (1.1f, 210.0f, 0.22f, 0.3f, 607)); } },
        { "perc-bongo", "Bongo", "Perc", [] { return mono (conga (0.5f, 400.0f, 0.085f, 0.4f, 608)); } },
        { "perc-woodblock", "Woodblock", "Perc", []
          {
              auto x = sine (0.2f, 850.0f);
              shape (x, [] (float t) { return std::exp (-t / 0.03f); });
              auto h = sine (0.2f, 2120.0f, 0.4f);
              shape (h, [] (float t) { return std::exp (-t / 0.012f); });
              mixInto (x, h, 0.5f);
              auto n = noise (0.02f, 609);
              bandpass (n, 1500.0f, 0.4f);
              shape (n, [] (float t) { return std::exp (-t / 0.002f); });
              normalise (n);
              mixInto (x, n, 0.3f);
              shape (x, [] (float t) { return attackEnv (t, 0.0003f); });
              return mono (x);
          } },
        { "perc-snap", "Finger Snap", "Perc", []
          {
              auto x = noise (0.15f, 610);
              bandpass (x, 2300.0f, 0.6f);
              shape (x, [] (float t) { return hit (t, 0.0004f, 0.012f); });
              normalise (x);
              auto tone = sine (0.15f, 1800.0f);
              shape (tone, [] (float t) { return std::exp (-t / 0.01f); });
              mixInto (x, tone, 0.35f);
              return mono (x);
          } },
        { "perc-triangle", "Triangle", "Perc", []
          {
              auto x = additive (2.5f, 1850.0f, { { 1.0f, 1.0f, 1.2f }, { 2.42f, 0.6f, 0.8f }, { 3.13f, 0.4f, 0.6f }, { 4.61f, 0.3f, 0.4f } }, 0.0005f, 611);
              shape (x, [] (float t) { return sustained (t, 0.0f, 2.5f, 0.3f); });
              return mono (x);
          } },

        // Cymbals
        { "cymbal-crash", "Crash", "Cymbal", [] { return crash (3.0f, 0.08f, 0.9f, 3500.0f, 701); } },
        { "cymbal-ride", "Ride", "Cymbal", []
          {
              auto c = crash (3.0f, 0.05f, 1.0f, 4500.0f, 702);
              for (juce::uint32 ch = 0; ch < 2; ++ch)
              {
                  auto& x = c[ch];
                  scale (x, 0.5f);
                  auto ping = additive (3.0f, 3150.0f, { { 1.0f, 1.0f, 1.0f }, { 1.5f, 0.6f, 0.8f }, { 1.97f, 0.4f, 0.5f } }, 0.0005f, 703 + ch);
                  normalise (ping, peakOf (x) * 0.9f);
                  mixInto (x, ping);
              }
              return c;
          } },
        { "cymbal-china", "China", "Cymbal", []
          {
              Channels out;
              for (juce::uint32 ch = 0; ch < 2; ++ch)
              {
                  auto x = metallic (2.5f, ch == 0 ? 2.6f : 2.63f, 704 + ch);
                  drive (x, 4.0f);
                  auto n = noise (2.5f, 706 + ch);
                  mixInto (x, n, 0.3f);
                  bandpass (x, 3000.0f, 0.3f);
                  highpass (x, 1200.0f);
                  shape (x, [] (float t) { return attackEnv (t, 0.001f) * (0.5f * std::exp (-t / 0.05f) + 0.5f * std::exp (-t / 0.7f)); });
                  out.push_back (std::move (x));
              }
              return out;
          } },
        { "cymbal-splash", "Splash", "Cymbal", [] { return crash (1.4f, 0.04f, 0.25f, 5000.0f, 708); } },
        { "cymbal-reverse", "Reverse Crash", "Cymbal", []
          {
              auto c = crash (2.4f, 0.08f, 0.8f, 3500.0f, 709);
              for (auto& x : c)
              {
                  std::reverse (x.begin(), x.end());
                  shape (x, [] (float t) { return juce::jmin (1.0f, t / 0.3f); });
              }
              return c;
          } },

        // FX
        { "fx-zap", "Zap", "FX", []
          {
              auto x = glideSine (0.35f, 3600.0f, 60.0f, 0.025f);
              shape (x, [] (float t) { return hit (t, 0.0005f, 0.07f); });
              return mono (x);
          } },
        { "fx-laser", "Laser", "FX", []
          {
              auto x = oscillator (1.0f, dsp::Oscillator::square, [] (float t)
              {
                  return (300.0f + 2600.0f * std::exp (-t / 0.09f)) * (1.0f + 0.05f * std::sin (twoPi * 35.0f * t));
              }, 0.5f);
              lowpass (x, 6000.0f);
              shape (x, [] (float t) { return hit (t, 0.001f, 0.2f); });
              return mono (x);
          } },
        { "fx-riser", "Riser", "FX", [] { return riser(); } },
        { "fx-noise-sweep", "Noise Sweep", "FX", [] { return noiseSweep(); } },
        { "fx-glitch", "Glitch", "FX", [] { return mono (glitch()); } },
        { "fx-vinyl", "Vinyl Crackle", "FX", [] { return vinyl(); } },
        { "fx-impact", "Impact", "FX", []
          {
              auto x = glideSine (3.0f, 110.0f, 32.0f, 0.3f);
              shape (x, [] (float t) { return hit (t, 0.001f, 0.55f); });
              auto n = noise (3.0f, 801);
              lowpass (n, 600.0f);
              shape (n, [] (float t) { return hit (t, 0.001f, 0.15f); });
              normalise (n);
              mixInto (x, n, 0.6f);
              drive (x, 2.0f);
              return mono (x);
          } },
        { "fx-blip", "Blip", "FX", []
          {
              auto x = sine (0.25f, 1500.0f);
              auto h = sine (0.25f, 3000.0f);
              mixInto (x, h, 0.3f);
              shape (x, [] (float t) { return hit (t, 0.001f, 0.04f); });
              return mono (x);
          } },

        // Tonal (C3)
        { "tonal-epiano", "Electric Piano", "Tonal", [] { return electricPiano(); } },
        { "tonal-pluck", "Pluck", "Tonal", [] { return pluck(); } },
        { "tonal-bell", "Bell", "Tonal", [] { return bell(); } },
        { "tonal-marimba", "Marimba", "Tonal", [] { return marimba(); } },
        { "tonal-vibes", "Vibes", "Tonal", [] { return vibes(); } },
        { "tonal-kalimba", "Kalimba", "Tonal", [] { return kalimba(); } },
        { "tonal-pad", "Soft Pad", "Tonal", [] { return softPad(); } },
        { "tonal-choir", "Choir", "Tonal", [] { return choir(); } },
        { "tonal-brass", "Brass Stab", "Tonal", [] { return brass(); } },
        { "tonal-strings", "String Ensemble", "Tonal", [] { return strings(); } },
        { "tonal-organ", "Organ", "Tonal", [] { return organ(); } },
        { "tonal-nylon", "Nylon Guitar", "Tonal", [] { return nylon(); } },
        { "tonal-bass", "Bass Pluck", "Tonal", [] { return bassPluck(); } },
        { "tonal-lead", "Synth Lead", "Tonal", [] { return lead(); } },
        { "tonal-flute", "Flute", "Tonal", [] { return flute(); } },
    };
    return table;
}

// DC block, normalise to -1 dBFS, trim the silent tail and fade the last few milliseconds.
SamplePtr finish (Channels channels, const juce::String& name)
{
    if (channels.empty() || channels[0].empty())
        return nullptr;

    size_t length = channels[0].size();
    for (auto& c : channels)
        length = juce::jmin (length, c.size());
    length = juce::jmin (length, numFrames (maxSeconds));

    const float r = 1.0f - twoPi * 10.0f / rate;
    float peak = 0.0f;
    for (auto& c : channels)
    {
        c.resize (length);
        float x1 = 0.0f, y1 = 0.0f;
        for (auto& s : c)
        {
            const float y = s - x1 + r * y1;
            x1 = s;
            y1 = y;
            s = y;
            peak = juce::jmax (peak, std::abs (y));
        }
    }
    if (peak <= 0.0f)
        return nullptr;

    const float gain = 0.891f / peak; // -1 dBFS
    size_t lastAudible = 0;
    for (auto& c : channels)
        for (size_t i = 0; i < c.size(); ++i)
        {
            c[i] *= gain;
            if (std::abs (c[i]) > 1.0e-4f)
                lastAudible = juce::jmax (lastAudible, i);
        }

    length = juce::jmin (length, juce::jmax ((size_t) 480, lastAudible + 1));
    const size_t fade = juce::jmin (length / 4, (size_t) 1920); // up to 40 ms, raised cosine

    auto sample = std::make_shared<SampleData>();
    sample->sampleRate = rate;
    sample->name = name;
    sample->buffer.setSize ((int) channels.size(), (int) length);
    for (size_t ch = 0; ch < channels.size(); ++ch)
    {
        float* dest = sample->buffer.getWritePointer ((int) ch);
        for (size_t i = 0; i < length; ++i)
        {
            const size_t fromEnd = length - 1 - i;
            const float g = fromEnd < fade ? 0.5f - 0.5f * std::cos (pi * (float) fromEnd / (float) fade) : 1.0f;
            dest[i] = channels[ch][i] * g;
        }
    }
    return sample;
}

} // namespace

const std::vector<Info>& list()
{
    static const std::vector<Info> sounds = []
    {
        std::vector<Info> result;
        for (auto& e : entries())
            result.push_back ({ e.id, e.name, e.category });
        return result;
    }();
    return sounds;
}

SamplePtr render (const juce::String& id)
{
    for (auto& e : entries())
        if (id == e.id)
            return finish (e.render(), e.name);
    return nullptr;
}

} // namespace rb::FactorySounds
