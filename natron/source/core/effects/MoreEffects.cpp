#include "MoreEffects.h"

#include <cmath>

namespace {
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    float coefficientFor(double seconds, double sampleRate) {
        return static_cast<float>(std::exp(-1.0 / (juce::jmax(1.0e-5, seconds) * sampleRate)));
    }

    // LFO shapes for Tremolo and AutoPan: sine, triangle, square. Output 0..1.
    float unipolarLfo(int shape, double phase) {
        switch (shape) {
            case 1: return static_cast<float>(phase < 0.5 ? 2.0 * phase : 2.0 - 2.0 * phase);
            case 2: return phase < 0.5 ? 1.0f : 0.0f;
            default: return static_cast<float>(0.5 + 0.5 * std::sin(twoPi * phase));
        }
    }

    // Reads a delay line `delay` samples (fractional) behind `write`.
    float readDelay(const std::vector<float>& line, int write, double delay) {
        const auto size = static_cast<int>(line.size());
        double position = write - delay;
        while (position < 0.0) {
            position += size;
        }
        const auto index = static_cast<int>(position);
        const auto fraction = static_cast<float>(position - index);
        const float a = line[static_cast<size_t>(index % size)];
        const float b = line[static_cast<size_t>((index + 1) % size)];
        return a + fraction * (b - a);
    }
}

// ------------------------------------------------------------------------------------------------ AutoWah

AutoWah::AutoWah() {
    parameters.add({ "sensitivity", "Sensitivity", 0.0f, 1.0f, 0.7f });
    parameters.add({ "baseFrequency", "Base", 100.0f, 2000.0f, 300.0f });
    parameters.add({ "range", "Range", 0.0f, 5.0f, 3.0f }); // octaves
    parameters.add({ "resonance", "Resonance", 0.0f, 1.0f, 0.6f });
    parameters.add({ "attack", "Attack", 0.001f, 0.1f, 0.005f });
    parameters.add({ "release", "Release", 0.01f, 1.0f, 0.15f });
    parameters.add({ "mix", "Mix", 0.0f, 1.0f, 1.0f });
    jassert(parameters.size() == numParameters);
}

void AutoWah::prepare(double newSampleRate, int maxBlockSize) {
    sampleRate = newSampleRate;
    filter.prepare({ sampleRate, static_cast<juce::uint32>(juce::jmax(1, maxBlockSize)), 2 });
    filter.setType(juce::dsp::StateVariableTPTFilterType::bandpass);
    reset();
}

void AutoWah::reset() {
    filter.reset();
    envelope = 0.0f;
}

void AutoWah::process(juce::AudioBuffer<float>& buffer, int numSamples) {
    const float attackCoefficient = coefficientFor(parameters.get(attack), sampleRate);
    const float releaseCoefficient = coefficientFor(parameters.get(release), sampleRate);
    const float sensitivityValue = parameters.get(sensitivity) * 4.0f;
    const float wet = parameters.get(mix);
    filter.setResonance(juce::jmap(parameters.get(resonance), 0.7f, 8.0f));
    float* left = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);

    for (int i = 0; i < numSamples; i++) {
        const float level = juce::jmax(std::abs(left[i]), std::abs(right[i]));
        envelope = level + (envelope - level) * (level > envelope ? attackCoefficient : releaseCoefficient);
        if (i % 16 == 0) {
            const double octaves = parameters.get(range) * juce::jmin(1.0f, envelope * sensitivityValue);
            filter.setCutoffFrequency(static_cast<float>(juce::jmin(sampleRate * 0.45, parameters.get(baseFrequency) * std::pow(2.0, octaves))));
        }
        const float l = filter.processSample(0, left[i]);
        const float r = filter.processSample(1, right[i]);
        left[i] += wet * (l - left[i]);
        right[i] += wet * (r - right[i]);
    }
}

// ------------------------------------------------------------------------------------------------ Limiter

Limiter::Limiter() {
    parameters.add({ "threshold", "Threshold", -30.0f, 0.0f, -6.0f }); // dB
    parameters.add({ "release", "Release", 10.0f, 1000.0f, 100.0f });  // ms
    parameters.add({ "makeup", "Makeup", 0.0f, 24.0f, 0.0f });         // dB
    jassert(parameters.size() == numParameters);
}

void Limiter::process(juce::AudioBuffer<float>& buffer, int numSamples) {
    const float ceiling = juce::Decibels::decibelsToGain(parameters.get(threshold));
    const float makeupGain = juce::Decibels::decibelsToGain(parameters.get(makeup));
    const float releaseCoefficient = coefficientFor(parameters.get(release) / 1000.0, sampleRate);
    float* left = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);
    for (int i = 0; i < numSamples; i++) {
        const float peak = juce::jmax(std::abs(left[i]), std::abs(right[i]));
        gain = 1.0f + (gain - 1.0f) * releaseCoefficient;
        if (peak * gain > ceiling) {
            gain = ceiling / peak;
        }
        left[i] *= gain * makeupGain;
        right[i] *= gain * makeupGain;
    }
}

// ------------------------------------------------------------------------------------------------ Vinyl

Vinyl::Vinyl() {
    parameters.add({ "crackle", "Crackle", 0.0f, 1.0f, 0.3f });
    parameters.add({ "hiss", "Hiss", 0.0f, 1.0f, 0.2f });
    parameters.add({ "wow", "Wow", 0.0f, 1.0f, 0.2f });
    parameters.add({ "tone", "Tone", 0.0f, 1.0f, 0.5f }); // how narrow the bandwidth is
    parameters.add({ "mix", "Mix", 0.0f, 1.0f, 1.0f });
    jassert(parameters.size() == numParameters);
}

void Vinyl::prepare(double newSampleRate, int) {
    sampleRate = newSampleRate;
    line.assign(static_cast<size_t>(2 * static_cast<int>(0.02 * sampleRate)), 0.0f);
    reset();
}

void Vinyl::reset() {
    std::fill(line.begin(), line.end(), 0.0f);
    write = 0;
    wowPhase = 0.0;
    lowPass.reset();
    highPass.reset();
    lastTone = -1.0f;
    crackleState = 0.0f;
    random.setSeed(1977);
}

void Vinyl::process(juce::AudioBuffer<float>& buffer, int numSamples) {
    const float toneValue = parameters.get(tone);
    if (!juce::exactlyEqual(toneValue, lastTone)) {
        lowPass.setLowPass(sampleRate, 18000.0 * std::pow(0.15, toneValue));
        highPass.setHighPass(sampleRate, 20.0 + 180.0 * toneValue);
        lastTone = toneValue;
    }
    const float crackleDensity = parameters.get(crackle) * 0.002f;
    const float hissLevel = parameters.get(hiss) * 0.02f;
    const double wowDepth = parameters.get(wow) * 0.002 * sampleRate; // up to 2 ms
    const float wet = parameters.get(mix);
    const int frames = static_cast<int>(line.size() / 2);
    float* left = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);

    for (int i = 0; i < numSamples; i++) {
        // Wow: a slow (0.55 Hz) wobble of a short delay.
        line[static_cast<size_t>(2 * write)] = left[i];
        line[static_cast<size_t>(2 * write + 1)] = right[i];
        const double delay = 1.0 + wowDepth * (0.5 + 0.5 * std::sin(twoPi * wowPhase));
        wowPhase += 0.55 / sampleRate;
        wowPhase -= std::floor(wowPhase);
        double position = write - delay;
        if (position < 0.0) {
            position += frames;
        }
        const auto index = static_cast<int>(position);
        const auto fraction = static_cast<float>(position - index);
        const int next = (index + 1) % frames;
        float l = line[static_cast<size_t>(2 * index)] + fraction * (line[static_cast<size_t>(2 * next)] - line[static_cast<size_t>(2 * index)]);
        float r = line[static_cast<size_t>(2 * index + 1)] + fraction * (line[static_cast<size_t>(2 * next + 1)] - line[static_cast<size_t>(2 * index + 1)]);
        write = (write + 1) % frames;

        // Crackle: sparse clicks that decay fast; hiss: a little noise.
        if (random.nextFloat() < crackleDensity) {
            crackleState = (random.nextFloat() * 2.0f - 1.0f) * 0.5f;
        }
        crackleState *= 0.6f;
        const float noise = (random.nextFloat() * 2.0f - 1.0f) * hissLevel + crackleState;

        l = highPass.process(0, lowPass.process(0, l)) + noise;
        r = highPass.process(1, lowPass.process(1, r)) + noise;
        left[i] += wet * (l - left[i]);
        right[i] += wet * (r - right[i]);
    }
}

// ------------------------------------------------------------------------------------------------ Comb / Static Flanger

void CombBase::prepare(double newSampleRate, int) {
    sampleRate = newSampleRate;
    for (auto& line : lines) {
        line.assign(static_cast<size_t>(0.06 * sampleRate) + 4, 0.0f); // up to 50 ms (20 Hz)
    }
    reset();
}

void CombBase::reset() {
    for (auto& line : lines) {
        std::fill(line.begin(), line.end(), 0.0f);
    }
    damped = {};
    write = 0;
}

void CombBase::process(juce::AudioBuffer<float>& buffer, int numSamples) {
    const auto size = static_cast<int>(lines[0].size());
    const double delay = juce::jlimit(1.0, size - 2.0, delaySeconds() * sampleRate);
    const float feedback = feedbackAmount();
    const float damping = dampingAmount();
    const float wet = mixAmount();

    for (int ch = 0; ch < 2; ch++) {
        float* data = buffer.getWritePointer(ch);
        auto& line = lines[static_cast<size_t>(ch)];
        int w = write;
        float state = damped[static_cast<size_t>(ch)];
        for (int i = 0; i < numSamples; i++) {
            const float delayed = readDelay(line, w, delay);
            state = delayed + damping * (state - delayed);
            const float out = data[i] + feedback * state;
            line[static_cast<size_t>(w)] = out;
            w = (w + 1) % size;
            data[i] += wet * (out - data[i]);
        }
        damped[static_cast<size_t>(ch)] = state;
    }
    write = (write + numSamples) % size;
}

Comb::Comb() {
    parameters.add({ "frequency", "Frequency", 20.0f, 2000.0f, 220.0f });
    parameters.add({ "feedback", "Feedback", -0.98f, 0.98f, 0.8f });
    parameters.add({ "damping", "Damping", 0.0f, 0.95f, 0.2f });
    parameters.add({ "mix", "Mix", 0.0f, 1.0f, 0.6f });
    jassert(parameters.size() == numParameters);
}

StaticFlanger::StaticFlanger() {
    parameters.add({ "delay", "Delay", 0.1f, 10.0f, 1.5f }); // ms
    parameters.add({ "feedback", "Feedback", -0.95f, 0.95f, 0.5f });
    parameters.add({ "mix", "Mix", 0.0f, 1.0f, 0.5f });
    jassert(parameters.size() == numParameters);
}

// ------------------------------------------------------------------------------------------------ Cabinet

Cabinet::Cabinet() {
    parameters.add({ "type", "Cabinet", 0.0f, 3.0f, 1.0f, 1.0f, { "1x12", "2x12", "4x12", "Radio" } });
    parameters.add({ "tone", "Tone", 0.0f, 1.0f, 0.5f });
    parameters.add({ "mix", "Mix", 0.0f, 1.0f, 1.0f });
    jassert(parameters.size() == numParameters);
}

void Cabinet::prepare(double newSampleRate, int) {
    sampleRate = newSampleRate;
    reset();
}

void Cabinet::reset() {
    for (auto& stage : stages) {
        stage.reset();
    }
    appliedType = -1;
}

void Cabinet::configure(int cabinet, float toneValue) {
    // High-pass, low-pass, a body resonance, and a presence peak: one voicing per cabinet.
    struct Voicing { double highPass, lowPass, bodyHz, bodyDb, presenceHz, presenceDb; };
    const Voicing voicings[] = {
        { 90.0, 5000.0, 120.0, 2.0, 2500.0, 3.0 },  // 1x12: open back, bright
        { 75.0, 4500.0, 110.0, 3.0, 2200.0, 2.0 },  // 2x12
        { 60.0, 3800.0, 100.0, 5.0, 1800.0, 1.0 },  // 4x12: big, dark
        { 300.0, 3000.0, 800.0, 4.0, 1500.0, 2.0 }, // radio: narrow and boxy
    };
    const auto& v = voicings[juce::jlimit(0, 3, cabinet)];
    const double lowPassHz = v.lowPass * std::pow(2.0, (toneValue - 0.5) * 1.5);
    stages[0].setHighPass(sampleRate, v.highPass);
    stages[1].setLowPass(sampleRate, juce::jmin(sampleRate * 0.45, lowPassHz), 0.8);
    stages[2].setPeak(sampleRate, v.bodyHz, 1.0, v.bodyDb);
    stages[3].setPeak(sampleRate, v.presenceHz, 1.2, v.presenceDb);
    appliedType = cabinet;
    appliedTone = toneValue;
}

void Cabinet::process(juce::AudioBuffer<float>& buffer, int numSamples) {
    const int cabinet = juce::roundToInt(parameters.get(type));
    const float toneValue = parameters.get(tone);
    if (cabinet != appliedType || !juce::exactlyEqual(toneValue, appliedTone)) {
        configure(cabinet, toneValue);
    }
    const float wet = parameters.get(mix);
    for (int ch = 0; ch < 2; ch++) {
        float* data = buffer.getWritePointer(ch);
        for (int i = 0; i < numSamples; i++) {
            float x = data[i];
            for (auto& stage : stages) {
                x = stage.process(ch, x);
            }
            data[i] += wet * (x - data[i]);
        }
    }
}

// ------------------------------------------------------------------------------------------------ Octaver

Octaver::Octaver() {
    parameters.add({ "octaveDown", "Octave", 0.0f, 1.0f, 0.7f });
    parameters.add({ "twoOctavesDown", "2 Octaves", 0.0f, 1.0f, 0.3f });
    parameters.add({ "dry", "Dry", 0.0f, 1.0f, 0.7f });
    parameters.add({ "tone", "Tone", 0.0f, 1.0f, 0.4f });
    jassert(parameters.size() == numParameters);
}

void Octaver::prepare(double newSampleRate, int) {
    sampleRate = newSampleRate;
    tracking.setLowPass(sampleRate, 900.0);
    reset();
}

void Octaver::reset() {
    tracking.reset();
    smoothing.reset();
    envelope = 0.0f;
    previous = 0.0f;
    divideBy2 = divideBy4 = 1.0f;
    lastTone = -1.0f;
}

void Octaver::process(juce::AudioBuffer<float>& buffer, int numSamples) {
    const float toneValue = parameters.get(tone);
    if (!juce::exactlyEqual(toneValue, lastTone)) {
        smoothing.setLowPass(sampleRate, 200.0 * std::pow(20.0, toneValue));
        lastTone = toneValue;
    }
    const float one = parameters.get(octaveDown);
    const float two = parameters.get(twoOctavesDown);
    const float dryLevel = parameters.get(dry);
    const float envelopeCoefficient = coefficientFor(0.02, sampleRate);
    float* left = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);

    for (int i = 0; i < numSamples; i++) {
        // The octaver tracks the mono sum (like a pedal tracks one string).
        const float input = 0.5f * (left[i] + right[i]);
        const float tracked = tracking.process(0, input);
        envelope = std::abs(input) + (envelope - std::abs(input)) * envelopeCoefficient;

        // Flip-flops: each rising zero crossing toggles /2; each /2 rising edge toggles /4.
        if (previous <= 0.0f && tracked > 0.0f) {
            divideBy2 = -divideBy2;
            if (divideBy2 > 0.0f) {
                divideBy4 = -divideBy4;
            }
        }
        previous = tracked;

        const float sub = smoothing.process(0, (one * divideBy2 + two * divideBy4) * envelope * 1.5f);
        left[i] = left[i] * dryLevel + sub;
        right[i] = right[i] * dryLevel + sub;
    }
}

// ------------------------------------------------------------------------------------------------ Vibrato

Vibrato::Vibrato() {
    parameters.add({ "rate", "Rate", 0.1f, 12.0f, 5.0f });   // Hz
    parameters.add({ "depth", "Depth", 0.0f, 1.0f, 0.4f });  // up to 5 ms of delay sweep
    jassert(parameters.size() == numParameters);
}

void Vibrato::prepare(double newSampleRate, int) {
    sampleRate = newSampleRate;
    for (auto& line : lines) {
        line.assign(static_cast<size_t>(0.012 * sampleRate) + 4, 0.0f);
    }
    reset();
}

void Vibrato::reset() {
    for (auto& line : lines) {
        std::fill(line.begin(), line.end(), 0.0f);
    }
    write = 0;
    phase = 0.0;
}

void Vibrato::process(juce::AudioBuffer<float>& buffer, int numSamples) {
    const auto size = static_cast<int>(lines[0].size());
    const double sweep = parameters.get(depth) * 0.005 * sampleRate;
    const double increment = parameters.get(rate) / sampleRate;
    double phaseNow = phase;
    for (int ch = 0; ch < 2; ch++) {
        float* data = buffer.getWritePointer(ch);
        auto& line = lines[static_cast<size_t>(ch)];
        int w = write;
        double p = phase;
        for (int i = 0; i < numSamples; i++) {
            line[static_cast<size_t>(w)] = data[i];
            const double delay = 1.0 + sweep * (0.5 + 0.5 * std::sin(twoPi * p));
            data[i] = readDelay(line, w, delay);
            w = (w + 1) % size;
            p += increment;
            p -= std::floor(p);
        }
        phaseNow = p;
    }
    phase = phaseNow;
    write = (write + numSamples) % size;
}

// ------------------------------------------------------------------------------------------------ Tremolo / AutoPan

Tremolo::Tremolo() {
    parameters.add({ "rate", "Rate", 0.1f, 20.0f, 5.0f });
    parameters.add({ "depth", "Depth", 0.0f, 1.0f, 0.6f });
    parameters.add({ "shape", "Shape", 0.0f, 2.0f, 0.0f, 1.0f, { "Sine", "Triangle", "Square" } });
    jassert(parameters.size() == numParameters);
}

void Tremolo::process(juce::AudioBuffer<float>& buffer, int numSamples) {
    const float depthValue = parameters.get(depth);
    const int shapeValue = juce::roundToInt(parameters.get(shape));
    const double increment = parameters.get(rate) / sampleRate;
    float* left = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);
    for (int i = 0; i < numSamples; i++) {
        const float gain = 1.0f - depthValue * unipolarLfo(shapeValue, phase);
        left[i] *= gain;
        right[i] *= gain;
        phase += increment;
        phase -= std::floor(phase);
    }
}

AutoPan::AutoPan() {
    parameters.add({ "rate", "Rate", 0.05f, 10.0f, 0.5f });
    parameters.add({ "depth", "Depth", 0.0f, 1.0f, 0.8f });
    parameters.add({ "shape", "Shape", 0.0f, 2.0f, 0.0f, 1.0f, { "Sine", "Triangle", "Square" } });
    jassert(parameters.size() == numParameters);
}

void AutoPan::process(juce::AudioBuffer<float>& buffer, int numSamples) {
    const float depthValue = parameters.get(depth);
    const int shapeValue = juce::roundToInt(parameters.get(shape));
    const double increment = parameters.get(rate) / sampleRate;
    float* left = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);
    for (int i = 0; i < numSamples; i++) {
        const float pan = depthValue * (2.0f * unipolarLfo(shapeValue, phase) - 1.0f); // -1 left ... +1 right
        left[i] *= juce::jmin(1.0f, 1.0f - pan);
        right[i] *= juce::jmin(1.0f, 1.0f + pan);
        phase += increment;
        phase -= std::floor(phase);
    }
}
