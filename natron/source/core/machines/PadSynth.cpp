#include "PadSynth.h"

#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace {
    using P = PadSynth::ParameterIndex;

    class Voice : public MachineVoice {
    public:
        Voice(const ParameterSet& parametersToUse, int index) :
            MachineVoice(parametersToUse), voiceIndex(index) {}

        void setTable(const PadSynth::Table* const* source) { table = source; }

        void prepare(double sampleRate, int) override { amp.setSampleRate(sampleRate); }

        void startNote(int midiNote, float velocity, juce::SynthesiserSound*, int) override {
            frequency = juce::MidiMessage::getMidiNoteInHertz(midiNote);
            gain = velocity;
            // Each voice starts somewhere different in the table, so stacked notes don't phase.
            position = std::fmod((voiceIndex * 0.137 + midiNote * 0.0311) * (1 << PadSynth::tableOrder),
                                 static_cast<double>(1 << PadSynth::tableOrder));
            amp.setParameters({ get(P::attack), get(P::decay), get(P::sustain), get(P::release) });
            amp.noteOn();
        }

        void stopNote(float, bool allowTailOff) override {
            if (allowTailOff) {
                amp.noteOff();
            } else {
                amp.reset();
                clearCurrentNote();
            }
        }

        void renderNextBlock(juce::AudioBuffer<float>& output, int startSample, int numSamples) override {
            const auto* current = table != nullptr ? *table : nullptr;
            if (!isVoiceActive() || current == nullptr) {
                return;
            }
            const auto& samples = current->samples;
            const auto size = static_cast<int>(samples.size());
            const double rate = frequency / PadSynth::tableFrequency * PadSynth::tableSampleRate / getSampleRate();
            const float level = get(P::level) * gain;
            const int rightChannel = output.getNumChannels() > 1 ? 1 : 0;

            for (int i = startSample; i < startSample + numSamples; i++) {
                const auto index = static_cast<int>(position);
                const auto fraction = static_cast<float>(position - index);
                // Left reads here, right half a table away: decorrelated stereo from one table.
                const int other = (index + size / 2) % size;
                const float left = samples[static_cast<size_t>(index)] + fraction * (samples[static_cast<size_t>((index + 1) % size)] - samples[static_cast<size_t>(index)]);
                const float right = samples[static_cast<size_t>(other)] + fraction * (samples[static_cast<size_t>((other + 1) % size)] - samples[static_cast<size_t>(other)]);
                position += rate;
                if (position >= size) {
                    position -= size;
                }
                const float envelope = amp.getNextSample() * level;
                output.addSample(0, i, left * envelope);
                if (rightChannel != 0) {
                    output.addSample(rightChannel, i, right * envelope);
                }
                if (!amp.isActive()) {
                    clearCurrentNote();
                    return;
                }
            }
        }

    private:
        int voiceIndex;
        const PadSynth::Table* const* table = nullptr;
        double frequency = 220.0;
        double position = 0.0;
        float gain = 1.0f;
        juce::ADSR amp;
    };
}

PadSynth::PadSynth() {
    const float defaults[numHarmonics] = { 1.0f, 0.6f, 0.45f, 0.3f, 0.25f, 0.15f, 0.1f, 0.08f };
    for (int h = 0; h < numHarmonics; h++) {
        parameters.add({ "harmonic" + juce::String(h + 1), "Harm " + juce::String(h + 1), 0.0f, 1.0f, defaults[h] });
    }
    parameters.add({ "bandwidth", "Bandwidth", 1.0f, 200.0f, 40.0f }); // cents, for the first harmonic
    parameters.add({ "bandwidthScale", "BW Scale", 0.0f, 2.0f, 1.0f }); // how bandwidth grows with harmonic number
    parameters.add({ "attack", "Attack", 0.001f, 8.0f, 0.6f });
    parameters.add({ "decay", "Decay", 0.001f, 8.0f, 1.0f });
    parameters.add({ "sustain", "Sustain", 0.0f, 1.0f, 0.8f });
    parameters.add({ "release", "Release", 0.001f, 8.0f, 1.2f });
    parameters.add({ "level", "Level", 0.0f, 1.0f, 0.5f });
    jassert(parameters.size() == numParameters);

    addVoices<Voice>(8);
    for (int i = 0; i < synth.getNumVoices(); i++) {
        static_cast<Voice*>(synth.getVoice(i))->setTable(&blockTable);
    }
    rebuild();
}

void PadSynth::process(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) {
    blockTable = table.acquire();
    VoiceMachine::process(output, midi);
}

void PadSynth::parameterChanged(int index) {
    if (index < 0 || index < numHarmonics || index == bandwidth || index == bandwidthScale) {
        rebuild();
    }
}

void PadSynth::rebuild() {
    float levels[numHarmonics];
    for (int h = 0; h < numHarmonics; h++) {
        levels[h] = parameters.get(h);
    }
    table.set(buildTable(levels, parameters.get(bandwidth), parameters.get(bandwidthScale)));
}

std::unique_ptr<PadSynth::Table> PadSynth::buildTable(const float* harmonicLevels, float bandwidthCents, float scale) {
    constexpr int size = 1 << tableOrder;
    constexpr int bins = size / 2;
    const double binHz = tableSampleRate / size;

    // Amplitude spectrum: each harmonic as a Gaussian of its own bandwidth.
    std::vector<double> amplitude(static_cast<size_t>(bins), 0.0);
    for (int h = 1; h <= numHarmonics; h++) {
        const double level = harmonicLevels[h - 1];
        if (level <= 0.0) {
            continue;
        }
        const double centre = tableFrequency * h;
        const double width = (std::pow(2.0, bandwidthCents / 1200.0) - 1.0) * tableFrequency * std::pow(h, scale);
        const double sigma = juce::jmax(binHz, width / 2.0);
        const int first = juce::jmax(1, static_cast<int>((centre - 4.0 * sigma) / binHz));
        const int last = juce::jmin(bins - 1, static_cast<int>((centre + 4.0 * sigma) / binHz) + 1);
        for (int k = first; k <= last; k++) {
            const double x = (k * binHz - centre) / sigma;
            amplitude[static_cast<size_t>(k)] += level * std::exp(-x * x) / sigma;
        }
    }

    // Random phases (fixed seed: the same table for the same settings), then inverse FFT.
    juce::dsp::FFT fft(tableOrder);
    std::vector<float> data(static_cast<size_t>(2 * size), 0.0f);
    juce::Random random(12345);
    for (int k = 1; k < bins; k++) {
        const double phase = random.nextDouble() * juce::MathConstants<double>::twoPi;
        data[static_cast<size_t>(2 * k)] = static_cast<float>(amplitude[static_cast<size_t>(k)] * std::cos(phase));
        data[static_cast<size_t>(2 * k + 1)] = static_cast<float>(amplitude[static_cast<size_t>(k)] * std::sin(phase));
    }
    fft.performRealOnlyInverseTransform(data.data());

    auto table = std::make_unique<Table>();
    table->samples.assign(data.begin(), data.begin() + size);
    float peak = 0.0f;
    for (const float x : table->samples) {
        peak = juce::jmax(peak, std::abs(x));
    }
    if (peak > 0.0f) {
        for (auto& x : table->samples) {
            x *= 0.7f / peak;
        }
    }
    return table;
}
