#include "AcidBass.h"

#include <cmath>

namespace {
    constexpr int controlInterval = 16;
    constexpr double ampAttackSeconds = 0.003;
    constexpr double ampReleaseSeconds = 0.008;
    constexpr double accentDecaySeconds = 0.2;
}

AcidBass::AcidBass() {
    parameters.add({ "shape", "Wave", 0.0f, 1.0f, 0.0f, 1.0f, { "Saw", "Square" } });
    parameters.add({ "tune", "Tune", -12.0f, 12.0f, 0.0f });
    parameters.add({ "cutoff", "Cutoff", 30.0f, 8000.0f, 350.0f });
    parameters.add({ "resonance", "Resonance", 0.0f, 1.0f, 0.65f });
    parameters.add({ "envMod", "Env Mod", 0.0f, 1.0f, 0.55f });
    parameters.add({ "decay", "Decay", 0.03f, 2.0f, 0.3f });
    parameters.add({ "accent", "Accent", 0.0f, 1.0f, 0.6f });
    parameters.add({ "slide", "Slide", 0.005f, 0.5f, 0.06f }); // glide time constant, seconds
    parameters.add({ "level", "Level", 0.0f, 1.0f, 0.5f });
    jassert(parameters.size() == numParameters);
}

void AcidBass::prepare(double newSampleRate, int) {
    sampleRate = newSampleRate;
    incrementPitch = -1.0e9;
    reset();
}

void AcidBass::reset() {
    numHeld = 0;
    gate = false;
    sliding = false;
    amp = 0.0f;
    filterEnvelope = 0.0f;
    accentEnvelope = 0.0f;
    ladder = {};
    oscillator.reset();
    controlCounter = 0;
}

void AcidBass::noteOn(int note, float velocity) {
    const bool legato = numHeld > 0;
    if (numHeld < static_cast<int>(held.size())) {
        held[static_cast<size_t>(numHeld++)] = note;
    }
    targetPitch = note;
    gate = true;

    if (legato) {
        sliding = true; // keep the envelopes running; glide to the new pitch
        return;
    }

    pitch = note;
    sliding = false;
    oscillator.reset(); // fresh notes start in phase, so renders are reproducible
    accented = velocity >= accentThreshold;
    filterEnvelope = 1.0f;
    if (accented) {
        // The 303 accent circuit stacks: repeated accents build up the sweep.
        accentEnvelope = juce::jmin(1.5f, accentEnvelope + 1.0f);
    }
}

void AcidBass::noteOff(int note) {
    for (int i = 0; i < numHeld; i++) {
        if (held[static_cast<size_t>(i)] == note) {
            for (int j = i; j < numHeld - 1; j++) {
                held[static_cast<size_t>(j)] = held[static_cast<size_t>(j + 1)];
            }
            numHeld--;
            break;
        }
    }

    if (numHeld == 0) {
        gate = false;
    } else if (juce::exactlyEqual(targetPitch, static_cast<double>(note))) {
        targetPitch = held[static_cast<size_t>(numHeld - 1)]; // fall back to the previous held note
        sliding = true;
    }
}

void AcidBass::process(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) {
    int position = 0;
    for (const auto metadata : midi) {
        const int eventPosition = juce::jlimit(0, output.getNumSamples(), metadata.samplePosition);
        render(output, position, eventPosition);
        position = eventPosition;

        const auto message = metadata.getMessage();
        if (message.isNoteOn()) {
            noteOn(message.getNoteNumber(), message.getFloatVelocity());
        } else if (message.isNoteOff()) {
            noteOff(message.getNoteNumber());
        } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
            numHeld = 0;
            gate = false;
        }
    }
    render(output, position, output.getNumSamples());
}

void AcidBass::render(juce::AudioBuffer<float>& output, int start, int end) {
    const auto waveform = parameters.get(shape) < 0.5f ? Oscillator::Shape::saw : Oscillator::Shape::square;
    const float k = 3.9f * parameters.get(resonance); // ladder feedback; ~4 self-oscillates
    const float levelValue = parameters.get(level);
    const float accentAmount = parameters.get(accent);

    const auto coefficient = [this](double seconds) {
        return static_cast<float>(std::exp(-1.0 / (seconds * sampleRate)));
    };
    const float decayCoefficient = coefficient(parameters.get(decay));
    const float accentCoefficient = coefficient(accentDecaySeconds);
    const float attackCoefficient = coefficient(ampAttackSeconds);
    const float releaseCoefficient = coefficient(ampReleaseSeconds);
    const double slideCoefficient = std::exp(-1.0 / (parameters.get(slide) * sampleRate));

    for (int i = start; i < end; i++) {
        if (sliding) {
            pitch = targetPitch + (pitch - targetPitch) * slideCoefficient;
        }

        if (controlCounter++ % controlInterval == 0) {
            const float sweep = filterEnvelope + (accented ? accentAmount * accentEnvelope : 0.0f);
            const double frequency = parameters.get(cutoff) * std::pow(2.0, 4.0 * parameters.get(envMod) * sweep);
            cutoffCoefficient = static_cast<float>(
                1.0 - std::exp(-juce::MathConstants<double>::twoPi * juce::jmin(frequency, sampleRate * 0.4) / sampleRate));
        }

        filterEnvelope *= decayCoefficient;
        accentEnvelope *= accentCoefficient;
        const float gateTarget = gate ? 1.0f : 0.0f;
        amp = gateTarget + (amp - gateTarget) * (gate ? attackCoefficient : releaseCoefficient);

        const double tunedPitch = pitch + parameters.get(tune);
        if (!juce::exactlyEqual(tunedPitch, incrementPitch)) { // pow only when the pitch moves (slides, tuning)
            incrementPitch = tunedPitch;
            increment = 440.0 * std::pow(2.0, (tunedPitch - 69.0) / 12.0) / sampleRate;
        }
        const float x = oscillator.next(waveform, increment);

        // 4-pole ladder with tanh saturation at the input.
        const float input = std::tanh(x - k * ladder[3]);
        ladder[0] += cutoffCoefficient * (input - ladder[0]);
        ladder[1] += cutoffCoefficient * (ladder[0] - ladder[1]);
        ladder[2] += cutoffCoefficient * (ladder[1] - ladder[2]);
        ladder[3] += cutoffCoefficient * (ladder[2] - ladder[3]);

        const float accentGain = accented ? 1.0f + 0.6f * accentAmount : 1.0f;
        const float sample = ladder[3] * (1.0f + 0.5f * k) * amp * accentGain * levelValue;

        for (int ch = 0; ch < output.getNumChannels(); ch++) {
            output.addSample(ch, i, sample);
        }
    }
}
