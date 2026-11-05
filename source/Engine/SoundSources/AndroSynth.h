#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

#include "../Devices.h"

// Zero-delay-feedback (TPT) state-variable filter, one channel. Used by the synth voice and the
// track filter effect.
struct StateVariableFilter {
    double ic1 = 0.0, ic2 = 0.0;

    void reset() { ic1 = ic2 = 0.0; }

    // Returns {low, high} for one sample. `resonance` 0..~0.95.
    std::pair<double, double> process(double input, double cutoff, double resonance, double sampleRate) {
        const double g = std::tan(juce::MathConstants<double>::pi * juce::jlimit(10.0, sampleRate * 0.45, cutoff) / sampleRate);
        const double k = 2.0 - 2.0 * juce::jlimit(0.0, 0.98, resonance);
        const double a1 = 1.0 / (1.0 + g * (g + k));
        const double v3 = input - ic2;
        const double v1 = a1 * ic1 + g * a1 * v3;
        const double v2 = ic2 + g * v1;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        const double low = v2;
        const double high = input - k * v1 - v2;
        return { low, high };
    }
};

// The Synth instrument: one oscillator (sine / saw / square / triangle) into a resonant low-pass
// filter with its own envelope amount, shaped by an ADSR. Filtered to one MIDI channel.
class AndroSynthSound : public juce::SynthesiserSound {
public:
    explicit AndroSynthSound(int midiChannel) : channel(midiChannel) {}
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int ch) override { return ch == channel; }

private:
    int channel;
};

class AndroSynthVoice : public juce::SynthesiserVoice {
public:
    explicit AndroSynthVoice(const DeviceParams& p) : params(p) {}

    bool canPlaySound(juce::SynthesiserSound* sound) override {
        return dynamic_cast<AndroSynthSound*>(sound) != nullptr;
    }

    void startNote(int midiNote, float velocity, juce::SynthesiserSound*, int) override {
        const int octave = static_cast<int>(params.get(SynthParam::Octave));
        frequency = juce::MidiMessage::getMidiNoteInHertz(juce::jlimit(0, 127, midiNote + 12 * octave));
        gain = velocity;
        phase = 0.0;
        filter.reset();
        envelope.setSampleRate(getSampleRate());
        envelope.setParameters({ params.get(SynthParam::Attack), params.get(SynthParam::Decay),
                                 params.get(SynthParam::Sustain), params.get(SynthParam::Release) });
        envelope.noteOn();
    }

    void stopNote(float, bool allowTailOff) override {
        if (allowTailOff) {
            envelope.noteOff();
        } else {
            envelope.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}

    void renderNextBlock(juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override {
        if (!isVoiceActive()) {
            return;
        }
        const double sampleRate = getSampleRate();
        const int wave = static_cast<int>(params.get(SynthParam::Wave));
        const double cutoff = params.get(SynthParam::Cutoff);
        const double resonance = params.get(SynthParam::Resonance);
        const double envAmount = params.get(SynthParam::FilterEnv);
        const double level = params.get(SynthParam::Level) * 0.25;
        const double delta = frequency / sampleRate;

        for (int i = 0; i < numSamples; i++) {
            double osc = 0.0;
            switch (wave) {
                case 0: osc = std::sin(juce::MathConstants<double>::twoPi * phase); break;
                case 1: osc = 2.0 * phase - 1.0; break;
                case 2: osc = phase < 0.5 ? 1.0 : -1.0; break;
                default: osc = 1.0 - 4.0 * std::abs(phase - 0.5); break;
            }
            phase += delta;
            if (phase >= 1.0) {
                phase -= 1.0;
            }

            const double env = envelope.getNextSample();
            // The filter opens with the envelope: up to +4 octaves at full amount.
            const double modulated = cutoff * std::pow(2.0, 4.0 * envAmount * env);
            const double sample = filter.process(osc, modulated, resonance, sampleRate).first * env * gain * level;

            for (int ch = 0; ch < buffer.getNumChannels(); ch++) {
                buffer.addSample(ch, startSample + i, static_cast<float>(sample));
            }

            if (!envelope.isActive()) {
                clearCurrentNote();
                break;
            }
        }
    }

private:
    const DeviceParams& params;
    juce::ADSR envelope;
    StateVariableFilter filter;
    double frequency = 440.0;
    double phase = 0.0;
    double gain = 1.0;
};
