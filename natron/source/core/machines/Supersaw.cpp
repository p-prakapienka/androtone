#include "Supersaw.h"

#include "../dsp/Oscillator.h"

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>

namespace {
    using P = Supersaw::ParameterIndex;
    constexpr int controlInterval = 16;

    // Detune offsets (semitones at full detune) and pan positions for each saw, centre first.
    constexpr std::array<double, Supersaw::maxSaws> offsets { 0.0, -0.11, 0.11, -0.25, 0.25, -0.42, 0.42, -0.6, 0.6 };
    constexpr std::array<float, Supersaw::maxSaws> pans { 0.0f, -0.4f, 0.4f, -0.7f, 0.7f, -0.9f, 0.9f, -1.0f, 1.0f };

    class Voice : public MachineVoice {
    public:
        Voice(const ParameterSet& parametersToUse, int index) :
            MachineVoice(parametersToUse), voiceIndex(index) {}

        void prepare(double sampleRate, int) override {
            for (auto& filter : filters) {
                filter.prepare({ sampleRate, static_cast<juce::uint32>(controlInterval), 1 });
            }
            amp.setSampleRate(sampleRate);
            filterEnvelope.setSampleRate(sampleRate);
        }

        void startNote(int midiNote, float velocity, juce::SynthesiserSound*, int) override {
            note = midiNote;
            gain = velocity;
            // Spread start phases (deterministically) so the stack doesn't start phase-locked.
            juce::Random random(voiceIndex * 7919 + midiNote);
            for (auto& osc : oscillators) {
                osc.reset(random.nextDouble());
            }
            for (auto& filter : filters) {
                filter.reset();
            }
            controlCounter = 0;
            amp.setParameters({ get(P::ampAttack), get(P::ampDecay), get(P::ampSustain), get(P::ampRelease) });
            filterEnvelope.setParameters({ get(P::filterAttack), get(P::filterDecay), get(P::filterSustain), get(P::filterRelease) });
            amp.noteOn();
            filterEnvelope.noteOn();
        }

        void stopNote(float, bool allowTailOff) override {
            if (allowTailOff) {
                amp.noteOff();
                filterEnvelope.noteOff();
            } else {
                amp.reset();
                clearCurrentNote();
            }
        }

        void renderNextBlock(juce::AudioBuffer<float>& output, int startSample, int numSamples) override {
            if (!isVoiceActive()) {
                return;
            }
            const int count = juce::jlimit(1, Supersaw::maxSaws, juce::roundToInt(get(P::saws)));
            const double base = juce::MidiMessage::getMidiNoteInHertz(note) / getSampleRate();
            const double spread = get(P::detune);
            const float side = get(P::sideMix);
            const float level = get(P::level) * gain / std::sqrt(static_cast<float>(count));

            std::array<double, Supersaw::maxSaws> increments {};
            std::array<float, Supersaw::maxSaws> leftGains {}, rightGains {};
            for (int s = 0; s < count; s++) {
                increments[static_cast<size_t>(s)] = base * std::pow(2.0, offsets[static_cast<size_t>(s)] * spread / 12.0);
                const float sawGain = s == 0 ? 1.0f - 0.5f * side : side;
                const float pan = pans[static_cast<size_t>(s)];
                leftGains[static_cast<size_t>(s)] = sawGain * juce::jmin(1.0f, 1.0f - pan);
                rightGains[static_cast<size_t>(s)] = sawGain * juce::jmin(1.0f, 1.0f + pan);
            }
            for (auto& filter : filters) {
                filter.setResonance(juce::jmap(get(P::resonance), 0.5f, 8.0f));
            }

            const int rightChannel = output.getNumChannels() > 1 ? 1 : 0;
            for (int i = startSample; i < startSample + numSamples; i++) {
                if (controlCounter++ % controlInterval == 0) {
                    const double cutoffHz = get(P::cutoff) * std::pow(2.0, 5.0 * get(P::filterEnvAmount) * filterLevel);
                    for (auto& filter : filters) {
                        filter.setCutoffFrequency(static_cast<float>(juce::jlimit(20.0, getSampleRate() * 0.45, cutoffHz)));
                    }
                }
                filterLevel = filterEnvelope.getNextSample();

                float left = 0.0f, right = 0.0f;
                for (int s = 0; s < count; s++) {
                    const float x = oscillators[static_cast<size_t>(s)].next(Oscillator::Shape::saw, increments[static_cast<size_t>(s)]);
                    left += x * leftGains[static_cast<size_t>(s)];
                    right += x * rightGains[static_cast<size_t>(s)];
                }
                const float envelope = amp.getNextSample() * level;
                output.addSample(0, i, filters[0].processSample(0, left) * envelope);
                if (rightChannel != 0) {
                    output.addSample(rightChannel, i, filters[1].processSample(0, right) * envelope);
                }
                if (!amp.isActive()) {
                    clearCurrentNote();
                    return;
                }
            }
        }

    private:
        int voiceIndex;
        int note = 60;
        float gain = 1.0f;
        float filterLevel = 0.0f;
        int controlCounter = 0;
        std::array<Oscillator, Supersaw::maxSaws> oscillators;
        std::array<juce::dsp::StateVariableTPTFilter<float>, 2> filters;
        juce::ADSR amp;
        juce::ADSR filterEnvelope;
    };
}

Supersaw::Supersaw() {
    parameters.add({ "saws", "Saws", 1.0f, static_cast<float>(maxSaws), 7.0f, 1.0f });
    parameters.add({ "detune", "Detune", 0.0f, 1.0f, 0.35f });
    parameters.add({ "sideMix", "Side Mix", 0.0f, 1.0f, 0.6f });
    parameters.add({ "cutoff", "Cutoff", 20.0f, 20000.0f, 6000.0f });
    parameters.add({ "resonance", "Resonance", 0.0f, 1.0f, 0.1f });
    parameters.add({ "filterEnvAmount", "Filter Env", -1.0f, 1.0f, 0.0f });
    parameters.add({ "filterAttack", "F Attack", 0.001f, 4.0f, 0.01f });
    parameters.add({ "filterDecay", "F Decay", 0.001f, 4.0f, 0.4f });
    parameters.add({ "filterSustain", "F Sustain", 0.0f, 1.0f, 0.5f });
    parameters.add({ "filterRelease", "F Release", 0.001f, 4.0f, 0.4f });
    parameters.add({ "ampAttack", "A Attack", 0.001f, 4.0f, 0.01f });
    parameters.add({ "ampDecay", "A Decay", 0.001f, 4.0f, 0.3f });
    parameters.add({ "ampSustain", "A Sustain", 0.0f, 1.0f, 0.8f });
    parameters.add({ "ampRelease", "A Release", 0.001f, 4.0f, 0.3f });
    parameters.add({ "level", "Level", 0.0f, 1.0f, 0.3f });
    jassert(parameters.size() == numParameters);
    addVoices<Voice>(8);
}
