#include "Pluck.h"

#include <cmath>
#include <vector>

namespace {
    using P = Pluck::ParameterIndex;
    constexpr double lowestHz = 20.0;

    class Voice : public MachineVoice {
    public:
        Voice(const ParameterSet& parametersToUse, int index) :
            MachineVoice(parametersToUse), random(1000 + index) {}

        void prepare(double sampleRate, int) override {
            line.assign(static_cast<size_t>(sampleRate / lowestHz) + 4, 0.0f);
        }

        void startNote(int midiNote, float velocity, juce::SynthesiserSound*, int) override {
            const double frequency = juce::MidiMessage::getMidiNoteInHertz(midiNote);
            // The two-point average in the loop adds half a sample of delay.
            delay = juce::jlimit(2.0, static_cast<double>(line.size() - 3), getSampleRate() / frequency - 0.5);
            const double period = delay + 0.5;
            const double t60 = get(P::decay);
            feedback = static_cast<float>(std::pow(10.0, -3.0 * period / (t60 * getSampleRate())));
            releasing = false;

            // Excite: a noise burst, low-passed by brightness, one period long.
            const auto coefficient = static_cast<float>(1.0 - get(P::brightness) * 0.95);
            float state = 0.0f;
            const int length = static_cast<int>(std::ceil(delay)) + 2;
            std::fill(line.begin(), line.end(), 0.0f);
            for (int i = 0; i < length; i++) {
                const float noise = random.nextFloat() * 2.0f - 1.0f;
                state += (1.0f - coefficient) * (noise - state);
                line[static_cast<size_t>(i)] = state * velocity;
            }
            write = length % static_cast<int>(line.size());
            previous = 0.0f;
            level = 1.0f;
            quietSamples = 0;
        }

        void stopNote(float, bool allowTailOff) override {
            if (allowTailOff) {
                releasing = true;
            } else {
                clearCurrentNote();
            }
        }

        void renderNextBlock(juce::AudioBuffer<float>& output, int startSample, int numSamples) override {
            if (!isVoiceActive()) {
                return;
            }
            const auto size = static_cast<int>(line.size());
            const float damping = get(P::damping);
            const float outputLevel = get(P::level);
            const auto releaseCoefficient = static_cast<float>(std::exp(-1.0 / (get(P::release) * getSampleRate())));

            float peak = 0.0f;
            for (int i = startSample; i < startSample + numSamples; i++) {
                // Fractional read `delay` samples behind the write head (linear interpolation).
                double readPosition = write - delay;
                if (readPosition < 0.0) {
                    readPosition += size;
                }
                const int index = static_cast<int>(readPosition);
                const auto fraction = static_cast<float>(readPosition - index);
                const float a = line[static_cast<size_t>(index)];
                const float b = line[static_cast<size_t>((index + 1) % size)];
                const float current = a + fraction * (b - a);

                // Damping blends the two-point average (a gentle low-pass) with the raw sample.
                const float averaged = 0.5f * (current + previous);
                const float filtered = current + damping * (averaged - current);
                previous = current;
                line[static_cast<size_t>(write)] = filtered * feedback;
                write = (write + 1) % size;

                if (releasing) {
                    level *= releaseCoefficient;
                }
                const float sample = current * level * outputLevel;
                peak = juce::jmax(peak, std::abs(sample));
                for (int ch = 0; ch < output.getNumChannels(); ch++) {
                    output.addSample(ch, i, sample);
                }
            }

            // Free the voice once the string has died away (or been released).
            quietSamples = peak < 1.0e-5f ? quietSamples + numSamples : 0;
            if (level < 1.0e-4f || quietSamples > static_cast<int>(getSampleRate() * 0.05)) {
                clearCurrentNote();
            }
        }

    private:
        std::vector<float> line;
        juce::Random random;
        double delay = 100.0;
        int write = 0;
        float feedback = 0.99f;
        float previous = 0.0f;
        float level = 1.0f;
        bool releasing = false;
        int quietSamples = 0;
    };
}

Pluck::Pluck() {
    parameters.add({ "decay", "Decay", 0.05f, 10.0f, 1.5f }); // seconds to -60 dB
    parameters.add({ "damping", "Damping", 0.0f, 1.0f, 0.5f });
    parameters.add({ "brightness", "Brightness", 0.0f, 1.0f, 0.7f });
    parameters.add({ "release", "Release", 0.01f, 2.0f, 0.15f });
    parameters.add({ "level", "Level", 0.0f, 1.0f, 0.6f });
    jassert(parameters.size() == numParameters);
    addVoices<Voice>(8);
}
