#include "Modular.h"

#include "../dsp/Oscillator.h"

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>

namespace {
    using P = Modular::ParameterIndex;
    constexpr int controlInterval = 16;

    // LFO value in [-1, 1] for a shape: sine, triangle, saw, square.
    float lfoValue(int shape, double phase) {
        switch (shape) {
            case 1: return static_cast<float>(phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase);
            case 2: return static_cast<float>(2.0 * phase - 1.0);
            case 3: return phase < 0.5 ? 1.0f : -1.0f;
            default: return static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * phase));
        }
    }

    class Voice : public MachineVoice {
    public:
        Voice(const ParameterSet& parametersToUse, int index) :
            MachineVoice(parametersToUse), noiseSource(900 + index), randomSource(31 + index) {}

        void prepare(double sampleRate, int) override {
            filter.prepare({ sampleRate, static_cast<juce::uint32>(controlInterval), 1 });
            envelope1.setSampleRate(sampleRate);
            envelope2.setSampleRate(sampleRate);
        }

        void startNote(int midiNote, float noteVelocity, juce::SynthesiserSound*, int) override {
            note = midiNote;
            velocityValue = noteVelocity;
            randomValue = randomSource.nextFloat() * 2.0f - 1.0f;
            osc1.reset();
            osc2.reset(0.3);
            lfoPhases = {};
            filter.reset();
            controlCounter = 0;
            envelope1.setParameters({ get(P::env1Attack), get(P::env1Decay), get(P::env1Sustain), get(P::env1Release) });
            envelope2.setParameters({ get(P::env2Attack), get(P::env2Decay), get(P::env2Sustain), get(P::env2Release) });
            envelope1.noteOn();
            envelope2.noteOn();
        }

        void stopNote(float, bool allowTailOff) override {
            if (allowTailOff) {
                envelope1.noteOff();
                envelope2.noteOff();
            } else {
                envelope1.reset();
                envelope2.reset();
                clearCurrentNote();
            }
        }

        void renderNextBlock(juce::AudioBuffer<float>& output, int startSample, int numSamples) override {
            if (!isVoiceActive()) {
                return;
            }
            const auto shape1 = Oscillator::shapeFromParameter(get(P::osc1Shape));
            const auto shape2 = Oscillator::shapeFromParameter(get(P::osc2Shape));
            const float outputLevel = get(P::level);
            filter.setType(juce::roundToInt(get(P::filterType)) == 1   ? juce::dsp::StateVariableTPTFilterType::highpass
                           : juce::roundToInt(get(P::filterType)) == 2 ? juce::dsp::StateVariableTPTFilterType::bandpass
                                                                       : juce::dsp::StateVariableTPTFilterType::lowpass);

            for (int i = startSample; i < startSample + numSamples; i++) {
                const float env1 = envelope1.getNextSample();
                const float env2 = envelope2.getNextSample();

                if (controlCounter++ % controlInterval == 0) {
                    updateControls(env1, env2);
                }

                const float oscillators = (1.0f - mix) * osc1.next(shape1, increment1) + mix * osc2.next(shape2, increment2);
                const float noiseSample = (noiseSource.nextFloat() * 2.0f - 1.0f) * noiseLevel;
                const float sample = filter.processSample(0, oscillators + noiseSample) * env1 * amp * outputLevel * velocityValue;
                for (int ch = 0; ch < output.getNumChannels(); ch++) {
                    output.addSample(ch, i, sample);
                }
                if (!envelope1.isActive()) {
                    clearCurrentNote();
                    return;
                }
            }
        }

    private:
        juce::Random noiseSource;
        juce::Random randomSource;
        Oscillator osc1;
        Oscillator osc2;
        juce::dsp::StateVariableTPTFilter<float> filter;
        juce::ADSR envelope1;
        juce::ADSR envelope2;
        std::array<double, 2> lfoPhases {};
        int note = 60;
        float velocityValue = 1.0f;
        float randomValue = 0.0f;
        int controlCounter = 0;
        double increment1 = 0.0;
        double increment2 = 0.0;
        float mix = 0.5f;
        float noiseLevel = 0.0f;
        float amp = 1.0f;

        float sourceValue(int source, float env1, float env2, const std::array<float, 2>& lfos) const {
            switch (source) {
                case Modular::envelope1: return env1;
                case Modular::envelope2: return env2;
                case Modular::lfo1: return lfos[0];
                case Modular::lfo2: return lfos[1];
                case Modular::velocity: return velocityValue;
                case Modular::keyTrack: return (note - 60) / 60.0f;
                case Modular::random: return randomValue;
                default: return 0.0f;
            }
        }

        void updateControls(float env1, float env2) {
            // Sum every cable into its destination, then apply the destinations.
            std::array<float, Modular::numDestinations> mod {};
            const double interval = controlInterval / getSampleRate();
            std::array<float, 2> lfos {
                lfoValue(juce::roundToInt(get(P::lfo1Shape)), lfoPhases[0]),
                lfoValue(juce::roundToInt(get(P::lfo2Shape)), lfoPhases[1]),
            };
            for (int patch = 0; patch < Modular::numPatches; patch++) {
                const int source = juce::roundToInt(get(Modular::patchParameter(patch, 0)));
                const int destination = juce::roundToInt(get(Modular::patchParameter(patch, 1)));
                if (source != Modular::none && destination > 0 && destination < Modular::numDestinations) {
                    mod[static_cast<size_t>(destination)] += sourceValue(source, env1, env2, lfos) * get(Modular::patchParameter(patch, 2));
                }
            }

            const double lfo1Rate = get(P::lfo1Rate) * std::pow(2.0, 4.0 * mod[Modular::lfo1RateDestination]);
            lfoPhases[0] = std::fmod(lfoPhases[0] + lfo1Rate * interval, 1.0);
            lfoPhases[1] = std::fmod(lfoPhases[1] + get(P::lfo2Rate) * interval, 1.0);

            const double baseHz = juce::MidiMessage::getMidiNoteInHertz(note);
            const double pitch1 = get(P::osc1Semitones) + 24.0 * (mod[Modular::osc1Pitch] + mod[Modular::bothPitch]);
            const double pitch2 = get(P::osc2Semitones) + get(P::osc2Fine) / 100.0 + 24.0 * (mod[Modular::osc2Pitch] + mod[Modular::bothPitch]);
            increment1 = baseHz * std::pow(2.0, pitch1 / 12.0) / getSampleRate();
            increment2 = baseHz * std::pow(2.0, pitch2 / 12.0) / getSampleRate();
            mix = juce::jlimit(0.0f, 1.0f, get(P::oscMix) + mod[Modular::oscMixDestination]);
            noiseLevel = juce::jlimit(0.0f, 1.0f, get(P::noise) + mod[Modular::noiseDestination]);
            amp = juce::jlimit(0.0f, 2.0f, 1.0f + mod[Modular::ampDestination]);

            const double cutoffHz = get(P::cutoff) * std::pow(2.0, 5.0 * mod[Modular::cutoffDestination]);
            filter.setCutoffFrequency(static_cast<float>(juce::jlimit(20.0, getSampleRate() * 0.45, cutoffHz)));
            filter.setResonance(juce::jmap(juce::jlimit(0.0f, 1.0f, get(P::resonance) + mod[Modular::resonanceDestination]), 0.5f, 10.0f));
        }
    };
}

Modular::Modular() {
    const juce::StringArray shapes { "Saw", "Square", "Triangle", "Sine" };
    const juce::StringArray lfoShapes { "Sine", "Triangle", "Saw", "Square" };
    const juce::StringArray sources { "None", "Env 1", "Env 2", "LFO 1", "LFO 2", "Velocity", "Key", "Random" };
    const juce::StringArray destinations { "None", "Osc 1 Pitch", "Osc 2 Pitch", "Pitch", "Osc Mix", "Cutoff",
                                           "Resonance", "Amp", "LFO 1 Rate", "Noise" };

    parameters.add({ "osc1Shape", "Osc 1", 0.0f, 3.0f, 0.0f, 1.0f, shapes });
    parameters.add({ "osc1Semitones", "Osc 1 Semi", -24.0f, 24.0f, 0.0f, 1.0f });
    parameters.add({ "osc2Shape", "Osc 2", 0.0f, 3.0f, 1.0f, 1.0f, shapes });
    parameters.add({ "osc2Semitones", "Osc 2 Semi", -24.0f, 24.0f, -12.0f, 1.0f });
    parameters.add({ "osc2Fine", "Osc 2 Fine", -50.0f, 50.0f, 5.0f });
    parameters.add({ "oscMix", "Osc Mix", 0.0f, 1.0f, 0.4f });
    parameters.add({ "noise", "Noise", 0.0f, 1.0f, 0.0f });
    parameters.add({ "filterType", "Filter", 0.0f, 2.0f, 0.0f, 1.0f, { "Low-pass", "High-pass", "Band-pass" } });
    parameters.add({ "cutoff", "Cutoff", 20.0f, 20000.0f, 900.0f });
    parameters.add({ "resonance", "Resonance", 0.0f, 1.0f, 0.3f });
    for (const auto* envelope : { "env1", "env2" }) {
        const juce::String name = juce::String(envelope).replace("env", "Env ");
        parameters.add({ juce::String(envelope) + "Attack", name + " A", 0.001f, 4.0f, 0.005f });
        parameters.add({ juce::String(envelope) + "Decay", name + " D", 0.001f, 4.0f, 0.4f });
        parameters.add({ juce::String(envelope) + "Sustain", name + " S", 0.0f, 1.0f, juce::String(envelope) == "env1" ? 0.7f : 0.0f });
        parameters.add({ juce::String(envelope) + "Release", name + " R", 0.001f, 4.0f, 0.3f });
    }
    parameters.add({ "lfo1Rate", "LFO 1 Rate", 0.05f, 20.0f, 5.0f });
    parameters.add({ "lfo1Shape", "LFO 1", 0.0f, 3.0f, 0.0f, 1.0f, lfoShapes });
    parameters.add({ "lfo2Rate", "LFO 2 Rate", 0.05f, 20.0f, 0.3f });
    parameters.add({ "lfo2Shape", "LFO 2", 0.0f, 3.0f, 1.0f, 1.0f, lfoShapes });

    // Default patch: envelope 2 sweeps the filter, LFO 2 drifts the oscillator mix.
    const int defaultSources[numPatches] = { envelope2, lfo2, none, none, none, none };
    const int defaultDestinations[numPatches] = { cutoffDestination, oscMixDestination, 0, 0, 0, 0 };
    const float defaultAmounts[numPatches] = { 0.5f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f };
    for (int patch = 0; patch < numPatches; patch++) {
        const juce::String n = juce::String(patch + 1);
        parameters.add({ "patch" + n + "Source", "Patch " + n + " From", 0.0f, numSources - 1.0f,
                         static_cast<float>(defaultSources[patch]), 1.0f, sources });
        parameters.add({ "patch" + n + "Destination", "Patch " + n + " To", 0.0f, numDestinations - 1.0f,
                         static_cast<float>(defaultDestinations[patch]), 1.0f, destinations });
        parameters.add({ "patch" + n + "Amount", "Patch " + n + " Amt", -1.0f, 1.0f, defaultAmounts[patch] });
    }
    parameters.add({ "level", "Level", 0.0f, 1.0f, 0.35f });
    jassert(parameters.size() == numParameters);
    addVoices<Voice>(8);
}
