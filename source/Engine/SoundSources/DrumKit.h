#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

#include "../Devices.h"

// The Drums instrument: eight synthesized drum sounds on the General MIDI drum note numbers
// (36 kick, 37 rim, 38 snare, 39 clap, 42 closed hat, 45 tom, 46 open hat, 56 bell). Other notes
// fall back to the nearest of these. Every sound is generated (no samples): sine sweeps, noise and
// simple filters. Tune and Decay scale all sounds; one-shot (note-off is ignored).
class DrumKitSound : public juce::SynthesiserSound {
public:
    explicit DrumKitSound(int midiChannel) : channel(midiChannel) {}
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int ch) override { return ch == channel; }

private:
    int channel;
};

class DrumKitVoice : public juce::SynthesiserVoice {
public:
    enum class Drum { Kick, Rim, Snare, Clap, ClosedHat, Tom, OpenHat, Bell };

    explicit DrumKitVoice(const DeviceParams& p) : params(p) {}

    static Drum drumFor(int note) {
        switch (note) {
            case 36: case 35: return Drum::Kick;
            case 37: return Drum::Rim;
            case 38: case 40: return Drum::Snare;
            case 39: return Drum::Clap;
            case 42: case 44: return Drum::ClosedHat;
            case 41: case 43: case 45: case 47: case 48: case 50: return Drum::Tom;
            case 46: return Drum::OpenHat;
            case 56: return Drum::Bell;
            default: return note < 38 ? Drum::Kick : (note < 42 ? Drum::Snare : (note < 50 ? Drum::ClosedHat : Drum::Bell));
        }
    }

    bool canPlaySound(juce::SynthesiserSound* sound) override {
        return dynamic_cast<DrumKitSound*>(sound) != nullptr;
    }

    void startNote(int midiNote, float velocity, juce::SynthesiserSound*, int) override {
        drum = drumFor(midiNote);
        gain = velocity * params.get(DrumParam::Level) * 0.5f;
        tune = params.get(DrumParam::Tune);
        decayScale = params.get(DrumParam::DecayScale);
        time = 0.0;
        phase = phase2 = 0.0;
        hp = lp = 0.0;
        previousNoise = 0.0;
        length = baseLength(drum) * decayScale;
    }

    void stopNote(float, bool allowTailOff) override {
        if (!allowTailOff) {
            clearCurrentNote();
        }
    }

    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}

    void renderNextBlock(juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override {
        if (!isVoiceActive()) {
            return;
        }
        const double dt = 1.0 / getSampleRate();
        for (int i = 0; i < numSamples; i++) {
            const double sample = next(dt) * gain;
            for (int ch = 0; ch < buffer.getNumChannels(); ch++) {
                buffer.addSample(ch, startSample + i, static_cast<float>(sample));
            }
            time += dt;
            if (time >= length) {
                clearCurrentNote();
                break;
            }
        }
    }

private:
    const DeviceParams& params;
    juce::Random random;
    Drum drum = Drum::Kick;
    double time = 0.0, length = 0.5, phase = 0.0, phase2 = 0.0;
    double hp = 0.0, lp = 0.0, previousNoise = 0.0;
    float gain = 1.0f, tune = 1.0f, decayScale = 1.0f;

    static double baseLength(Drum d) {
        switch (d) {
            case Drum::Kick: return 0.5;
            case Drum::Rim: return 0.05;
            case Drum::Snare: return 0.25;
            case Drum::Clap: return 0.3;
            case Drum::ClosedHat: return 0.08;
            case Drum::Tom: return 0.45;
            case Drum::OpenHat: return 0.45;
            case Drum::Bell: return 0.35;
        }
        return 0.3;
    }

    double expDecay(double seconds) const {
        return std::exp(-time / juce::jmax(1.0e-4, seconds * decayScale));
    }

    double noise() {
        return random.nextDouble() * 2.0 - 1.0;
    }

    // One-pole high-pass over white noise (brighter metallic noise for hats).
    double brightNoise(double coefficient) {
        const double n = noise();
        hp = coefficient * (hp + n - previousNoise);
        previousNoise = n;
        return hp;
    }

    double sine(double& ph, double frequency, double dt) {
        ph += frequency * dt;
        ph -= std::floor(ph);
        return std::sin(juce::MathConstants<double>::twoPi * ph);
    }

    double next(double dt) {
        switch (drum) {
            case Drum::Kick: {
                const double f = (50.0 + 110.0 * std::exp(-time / 0.04)) * tune;
                return sine(phase, f, dt) * expDecay(0.18) * 1.6;
            }
            case Drum::Tom: {
                const double f = (95.0 + 60.0 * std::exp(-time / 0.08)) * tune;
                return sine(phase, f, dt) * expDecay(0.16) * 1.2;
            }
            case Drum::Snare: {
                const double body = sine(phase, 185.0 * tune, dt) * expDecay(0.05);
                lp += 0.35 * (noise() - lp);
                return body * 0.7 + lp * expDecay(0.08) * 1.1;
            }
            case Drum::Clap: {
                // Three quick bursts, then a short tail.
                const double t = time;
                const double bursts = (t < 0.03) ? std::exp(-std::fmod(t, 0.01) / 0.002) : 0.0;
                const double tail = t >= 0.02 ? std::exp(-(t - 0.02) / (0.07 * decayScale)) : 0.0;
                lp += 0.5 * (brightNoise(0.6) - lp);
                return lp * (bursts + tail) * 1.4;
            }
            case Drum::ClosedHat:
                return brightNoise(0.92) * expDecay(0.018) * 0.9;
            case Drum::OpenHat:
                return brightNoise(0.92) * expDecay(0.12) * 0.8;
            case Drum::Rim:
                return (sine(phase, 1700.0 * tune, dt) * 0.6 + brightNoise(0.8) * 0.4) * expDecay(0.01) * 1.2;
            case Drum::Bell: {
                // Two detuned squares through a soft decay.
                phase += 560.0 * tune * dt;
                phase2 += 845.0 * tune * dt;
                phase -= std::floor(phase);
                phase2 -= std::floor(phase2);
                const double a = phase < 0.5 ? 1.0 : -1.0;
                const double b = phase2 < 0.5 ? 1.0 : -1.0;
                lp += 0.25 * ((a + b) * 0.5 - lp);
                return lp * expDecay(0.09) * 0.8;
            }
        }
        return 0.0;
    }
};
