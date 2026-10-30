#include "Organ.h"

#include <array>
#include <cmath>

namespace {
    using P = Organ::ParameterIndex;
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    class Voice : public MachineVoice {
    public:
        Voice(const ParameterSet& parametersToUse, int index) :
            MachineVoice(parametersToUse), random(500 + index) {}

        // The machine's vibrato offset in semitones, updated once per block before rendering.
        void setVibrato(const double* semitones) { vibrato = semitones; }

        void startNote(int midiNote, float velocity, juce::SynthesiserSound*, int) override {
            frequency = juce::MidiMessage::getMidiNoteInHertz(midiNote);
            phases = {};
            gain = 0.6f + 0.4f * velocity;
            gate = true;
            amp = 0.0f;
            percussionEnvelope = get(P::percussion) >= 0.5f ? 1.0f : 0.0f;
            clickSamples = static_cast<int>(getSampleRate() * 0.004 * get(P::click) * 4.0);
        }

        void stopNote(float, bool allowTailOff) override {
            gate = false;
            if (!allowTailOff) {
                clearCurrentNote();
            }
        }

        void renderNextBlock(juce::AudioBuffer<float>& output, int startSample, int numSamples) override {
            if (!isVoiceActive()) {
                return;
            }
            const double sampleRate = getSampleRate();
            const double vibratoRatio = std::pow(2.0, (vibrato != nullptr ? *vibrato : 0.0) / 12.0);

            std::array<float, Organ::numDrawbars> levels {};
            float total = 0.0f;
            for (int d = 0; d < Organ::numDrawbars; d++) {
                levels[static_cast<size_t>(d)] = get(d) / 8.0f;
                total += levels[static_cast<size_t>(d)];
            }
            const float normalise = 1.0f / juce::jmax(1.0f, total);
            const double percussionRatio = get(P::percussionHarmonic) >= 0.5f ? 3.0 : 2.0;
            const auto percussionDecay = static_cast<float>(std::exp(-1.0 / ((get(P::percussionDecay) >= 0.5f ? 0.5 : 0.2) * sampleRate)));
            const float percussionLevel = get(P::percussionLevel);
            const auto attack = static_cast<float>(1.0 - std::exp(-1.0 / (0.004 * sampleRate)));
            const auto releaseCoefficient = static_cast<float>(std::exp(-1.0 / (0.012 * sampleRate)));
            const float clickLevel = get(P::click) * 0.3f;

            for (int i = startSample; i < startSample + numSamples; i++) {
                float sample = 0.0f;
                for (int d = 0; d < Organ::numDrawbars; d++) {
                    if (levels[static_cast<size_t>(d)] <= 0.0f) {
                        continue;
                    }
                    auto& phase = phases[static_cast<size_t>(d)];
                    sample += levels[static_cast<size_t>(d)] * static_cast<float>(std::sin(twoPi * phase));
                    phase += frequency * Organ::drawbarRatios[d] * vibratoRatio / sampleRate;
                    phase -= std::floor(phase);
                }
                sample *= normalise;

                if (percussionEnvelope > 1.0e-4f) {
                    sample += percussionLevel * percussionEnvelope * static_cast<float>(std::sin(twoPi * percussionPhase));
                    percussionPhase += frequency * percussionRatio / sampleRate;
                    percussionPhase -= std::floor(percussionPhase);
                    percussionEnvelope *= percussionDecay;
                }
                if (clickSamples > 0) {
                    sample += clickLevel * (random.nextFloat() * 2.0f - 1.0f);
                    clickSamples--;
                }

                amp = gate ? amp + attack * (1.0f - amp) : amp * releaseCoefficient;
                const float out = sample * amp * gain;
                for (int ch = 0; ch < output.getNumChannels(); ch++) {
                    output.addSample(ch, i, out);
                }
            }
            if (!gate && amp < 1.0e-4f) {
                clearCurrentNote();
            }
        }

    private:
        const double* vibrato = nullptr;
        juce::Random random;
        double frequency = 440.0;
        std::array<double, Organ::numDrawbars> phases {};
        double percussionPhase = 0.0;
        float percussionEnvelope = 0.0f;
        float gain = 1.0f;
        float amp = 0.0f;
        bool gate = false;
        int clickSamples = 0;
    };
}

Organ::Organ() {
    const float defaults[numDrawbars] = { 8, 8, 8, 0, 0, 0, 0, 0, 0 }; // the classic 888000000
    const char* names[numDrawbars] = { "16'", "5 1/3'", "8'", "4'", "2 2/3'", "2'", "1 3/5'", "1 1/3'", "1'" };
    for (int d = 0; d < numDrawbars; d++) {
        parameters.add({ "drawbar" + juce::String(d + 1), names[d], 0.0f, 8.0f, defaults[d], 1.0f });
    }
    parameters.add({ "percussion", "Percussion", 0.0f, 1.0f, 0.0f, 1.0f });
    parameters.add({ "percussionHarmonic", "Perc 3rd", 0.0f, 1.0f, 1.0f, 1.0f }); // off = 2nd, on = 3rd
    parameters.add({ "percussionDecay", "Perc Slow", 0.0f, 1.0f, 0.0f, 1.0f });  // off = fast, on = slow
    parameters.add({ "percussionLevel", "Perc Level", 0.0f, 1.0f, 0.5f });
    parameters.add({ "click", "Click", 0.0f, 1.0f, 0.3f });
    parameters.add({ "vibratoDepth", "Vibrato", 0.0f, 1.0f, 0.0f });
    parameters.add({ "vibratoRate", "Vib Rate", 1.0f, 10.0f, 6.5f });
    parameters.add({ "rotarySpeed", "Rotary", 0.0f, 2.0f, 1.0f, 1.0f }); // off, slow, fast
    parameters.add({ "rotaryDepth", "Rot Depth", 0.0f, 1.0f, 0.5f });
    parameters.add({ "level", "Level", 0.0f, 1.0f, 0.4f });
    jassert(parameters.size() == numParameters);
    addVoices<Voice>(16);
    for (int i = 0; i < synth.getNumVoices(); i++) {
        static_cast<Voice*>(synth.getVoice(i))->setVibrato(&vibratoSemitones);
    }
}

void Organ::prepare(double newSampleRate, int maxBlockSize) {
    sampleRate = newSampleRate;
    VoiceMachine::prepare(newSampleRate, maxBlockSize);
}

void Organ::process(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) {
    const int numSamples = output.getNumSamples();

    // Vibrato: one value per block (up to +-0.3 semitone), shared by all voices.
    vibratoSemitones = parameters.get(vibratoDepth) * 0.3 * std::sin(twoPi * vibratoPhase);
    vibratoPhase = std::fmod(vibratoPhase + parameters.get(vibratoRate) * numSamples / sampleRate, 1.0);

    VoiceMachine::process(output, midi);

    // Rotary speaker: the horn sweeps the sound around the stereo field. The rotor speeds up and
    // slows down gradually, like the real thing.
    const int speed = juce::roundToInt(parameters.get(rotarySpeed));
    const double targetRate = speed == 0 ? 0.0 : speed == 1 ? 0.8 : 6.7;
    const float depth = parameters.get(rotaryDepth);
    if (speed == 0 && rotorRate < 1.0e-3) {
        return;
    }
    const double easing = std::exp(-1.0 / (0.8 * sampleRate));
    float* left = output.getWritePointer(0);
    float* right = output.getNumChannels() > 1 ? output.getWritePointer(1) : nullptr;
    for (int i = 0; i < numSamples; i++) {
        rotorRate = targetRate + (rotorRate - targetRate) * easing;
        rotorPhase += rotorRate / sampleRate;
        rotorPhase -= std::floor(rotorPhase);
        const auto sweep = static_cast<float>(std::sin(twoPi * rotorPhase));
        const float mono = left[i];
        left[i] = mono * (1.0f - 0.5f * depth * (1.0f + sweep) * 0.6f);
        if (right != nullptr) {
            right[i] = mono * (1.0f - 0.5f * depth * (1.0f - sweep) * 0.6f);
        }
    }
}
