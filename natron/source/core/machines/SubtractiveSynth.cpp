#include "SubtractiveSynth.h"

#include "../dsp/Oscillator.h"

#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace {
    using P = SubtractiveSynth::ParameterIndex;

    // Filter cutoff and envelopes are recomputed this often (samples); cheap and smooth enough.
    constexpr int controlInterval = 16;

    struct Sound : juce::SynthesiserSound {
        bool appliesToNote(int) override { return true; }
        bool appliesToChannel(int) override { return true; }
    };

    class Voice : public juce::SynthesiserVoice {
    public:
        explicit Voice(const ParameterSet& parametersToUse) :
            parameters(parametersToUse) {}

        void prepare(double sampleRate) {
            filter.prepare({ sampleRate, static_cast<juce::uint32>(controlInterval), 1 });
            ampEnvelope.setSampleRate(sampleRate);
            filterEnvelope.setSampleRate(sampleRate);
        }

        bool canPlaySound(juce::SynthesiserSound* sound) override { return dynamic_cast<Sound*>(sound) != nullptr; }

        void startNote(int midiNote, float velocity, juce::SynthesiserSound*, int) override {
            note = midiNote;
            gain = velocity;
            osc1.reset();
            osc2.reset(0.25); // offset so a unison patch doesn't start fully phase-locked
            filter.reset();
            controlCounter = 0;
            filterLevel = 0.0f;
            ampEnvelope.setParameters({ get(P::ampAttack), get(P::ampDecay), get(P::ampSustain), get(P::ampRelease) });
            filterEnvelope.setParameters(
                { get(P::filterAttack), get(P::filterDecay), get(P::filterSustain), get(P::filterRelease) });
            ampEnvelope.noteOn();
            filterEnvelope.noteOn();
        }

        void stopNote(float, bool allowTailOff) override {
            if (allowTailOff) {
                ampEnvelope.noteOff();
                filterEnvelope.noteOff();
            } else {
                ampEnvelope.reset();
                filterEnvelope.reset();
                clearCurrentNote();
            }
        }

        void pitchWheelMoved(int) override {}
        void controllerMoved(int, int) override {}

        // The machine's per-sample LFO for the current block (indexed like the output buffer).
        void setLfoBuffer(const float* values) { lfoBuffer = values; }

        void renderNextBlock(juce::AudioBuffer<float>& output, int startSample, int numSamples) override {
            if (!isVoiceActive()) {
                return;
            }

            const auto shape1 = Oscillator::shapeFromParameter(get(P::osc1Shape));
            const auto shape2 = Oscillator::shapeFromParameter(get(P::osc2Shape));
            const float mix = get(P::oscMix);
            const float level = get(P::level);
            const double sampleRate = getSampleRate();

            filter.setType(filterTypeFromParameter(get(P::filterType)));
            filter.setResonance(juce::jmap(get(P::resonance), 0.5f, 10.0f));

            for (int i = startSample; i < startSample + numSamples; i++) {
                // Control-rate updates on the voice's own clock, so results don't depend on how the
                // host or the MIDI events split the block.
                if (controlCounter++ % controlInterval == 0) {
                    const double lfo = lfoBuffer != nullptr ? lfoBuffer[i] : 0.0;
                    const double semitones = get(P::lfoToPitch) * lfo;
                    increment1 = juce::MidiMessage::getMidiNoteInHertz(note) * std::pow(2.0, semitones / 12.0) / sampleRate;
                    increment2 = increment1 * std::pow(2.0, (get(P::osc2Semitones) + get(P::osc2Fine) / 100.0) / 12.0);

                    // Envelope amount and LFO move the cutoff in octaves.
                    const double octaves = get(P::filterEnvAmount) * 5.0 * filterLevel + get(P::lfoToCutoff) * 2.0 * lfo;
                    filter.setCutoffFrequency(static_cast<float>(
                        juce::jlimit(20.0, sampleRate * 0.45, get(P::cutoff) * std::pow(2.0, octaves))));
                }
                filterLevel = filterEnvelope.getNextSample();

                const float oscillators = (1.0f - mix) * osc1.next(shape1, increment1) + mix * osc2.next(shape2, increment2);
                const float sample = filter.processSample(0, oscillators) * ampEnvelope.getNextSample() * gain * level;

                for (int ch = 0; ch < output.getNumChannels(); ch++) {
                    output.addSample(ch, i, sample);
                }

                if (!ampEnvelope.isActive()) {
                    clearCurrentNote();
                    return;
                }
            }
        }

    private:
        const ParameterSet& parameters;
        Oscillator osc1;
        Oscillator osc2;
        juce::dsp::StateVariableTPTFilter<float> filter;
        juce::ADSR ampEnvelope;
        juce::ADSR filterEnvelope;
        const float* lfoBuffer = nullptr;
        int note = 60;
        float gain = 1.0f;
        float filterLevel = 0.0f;
        int controlCounter = 0;
        double increment1 = 0.0;
        double increment2 = 0.0;

        float get(int index) const { return parameters.get(index); }

        static juce::dsp::StateVariableTPTFilterType filterTypeFromParameter(float value) {
            switch (juce::roundToInt(value)) {
                case 1: return juce::dsp::StateVariableTPTFilterType::highpass;
                case 2: return juce::dsp::StateVariableTPTFilterType::bandpass;
                default: return juce::dsp::StateVariableTPTFilterType::lowpass;
            }
        }
    };
}

SubtractiveSynth::SubtractiveSynth() {
    parameters.add({ "osc1Shape", "Osc 1 Shape", 0.0f, 3.0f, 0.0f, 1.0f, { "Saw", "Square", "Triangle", "Sine" } });
    parameters.add({ "osc2Shape", "Osc 2 Shape", 0.0f, 3.0f, 1.0f, 1.0f, { "Saw", "Square", "Triangle", "Sine" } });
    parameters.add({ "osc2Semitones", "Osc 2 Semi", -24.0f, 24.0f, 0.0f, 1.0f });
    parameters.add({ "osc2Fine", "Osc 2 Fine", -50.0f, 50.0f, 7.0f }); // cents
    parameters.add({ "oscMix", "Osc Mix", 0.0f, 1.0f, 0.5f });
    parameters.add({ "filterType", "Filter Type", 0.0f, 2.0f, 0.0f, 1.0f, { "Low-pass", "High-pass", "Band-pass" } });
    parameters.add({ "cutoff", "Cutoff", 20.0f, 20000.0f, 1800.0f });
    parameters.add({ "resonance", "Resonance", 0.0f, 1.0f, 0.2f });
    parameters.add({ "filterEnvAmount", "Filter Env", -1.0f, 1.0f, 0.3f });
    parameters.add({ "filterAttack", "F Attack", 0.001f, 4.0f, 0.005f });
    parameters.add({ "filterDecay", "F Decay", 0.001f, 4.0f, 0.3f });
    parameters.add({ "filterSustain", "F Sustain", 0.0f, 1.0f, 0.2f });
    parameters.add({ "filterRelease", "F Release", 0.001f, 4.0f, 0.3f });
    parameters.add({ "ampAttack", "A Attack", 0.001f, 4.0f, 0.005f });
    parameters.add({ "ampDecay", "A Decay", 0.001f, 4.0f, 0.2f });
    parameters.add({ "ampSustain", "A Sustain", 0.0f, 1.0f, 0.8f });
    parameters.add({ "ampRelease", "A Release", 0.001f, 4.0f, 0.2f });
    parameters.add({ "lfoRate", "LFO Rate", 0.05f, 20.0f, 4.0f }); // Hz
    parameters.add({ "lfoToCutoff", "LFO > Cutoff", 0.0f, 1.0f, 0.0f });
    parameters.add({ "lfoToPitch", "LFO > Pitch", 0.0f, 1.0f, 0.0f }); // semitones
    parameters.add({ "level", "Level", 0.0f, 1.0f, 0.3f });
    jassert(parameters.size() == numParameters);

    for (int i = 0; i < numVoices; i++) {
        synth.addVoice(new Voice(parameters));
    }
    synth.addSound(new Sound());
}

void SubtractiveSynth::prepare(double sampleRate, int maxBlockSize) {
    synth.setCurrentPlaybackSampleRate(sampleRate);
    lfoValues.assign(static_cast<size_t>(juce::jmax(1, maxBlockSize)), 0.0f);
    for (int i = 0; i < synth.getNumVoices(); i++) {
        static_cast<Voice*>(synth.getVoice(i))->prepare(sampleRate);
    }
    lfoPhase = 0.0;
}

void SubtractiveSynth::process(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) {
    // One free-running LFO shared by all voices, computed per sample.
    const int numSamples = juce::jmin(output.getNumSamples(), static_cast<int>(lfoValues.size()));
    const double increment = parameters.get(lfoRate) / synth.getSampleRate();
    for (int i = 0; i < numSamples; i++) {
        lfoValues[static_cast<size_t>(i)] = static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * lfoPhase));
        lfoPhase += increment;
        if (lfoPhase >= 1.0) {
            lfoPhase -= 1.0;
        }
    }
    for (int i = 0; i < synth.getNumVoices(); i++) {
        static_cast<Voice*>(synth.getVoice(i))->setLfoBuffer(lfoValues.data());
    }

    synth.renderNextBlock(output, midi, 0, numSamples);
}

void SubtractiveSynth::reset() {
    synth.allNotesOff(0, false);
}
