#include "Vocoder.h"

#include "../dsp/Oscillator.h"

#include <cmath>

namespace {
    using P = Vocoder::ParameterIndex;
    const juce::Identifier dataType { "Vocoder" };
    constexpr double lowestBandHz = 120.0;
    constexpr double highestBandHz = 7000.0;
    constexpr double bandQ = 6.0;

    double bandFrequency(int band) {
        return lowestBandHz * std::pow(highestBandHz / lowestBandHz, band / static_cast<double>(Vocoder::numBands - 1));
    }

    // The carrier voices: a saw (mixed with noise in the machine) with a simple attack/release.
    class Voice : public MachineVoice {
    public:
        Voice(const ParameterSet& parametersToUse, int index) :
            MachineVoice(parametersToUse), random(77 + index) {}

        void prepare(double sampleRate, int) override { amp.setSampleRate(sampleRate); }

        void startNote(int midiNote, float velocity, juce::SynthesiserSound*, int) override {
            increment = juce::MidiMessage::getMidiNoteInHertz(midiNote) / getSampleRate();
            gain = velocity;
            oscillator.reset();
            amp.setParameters({ get(P::attack), 0.0f, 1.0f, get(P::release) });
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
            if (!isVoiceActive()) {
                return;
            }
            const float noiseMix = get(P::noise);
            for (int i = startSample; i < startSample + numSamples; i++) {
                const float saw = oscillator.next(Oscillator::Shape::saw, increment);
                const float noise = random.nextFloat() * 2.0f - 1.0f;
                output.addSample(0, i, ((1.0f - noiseMix) * saw + noiseMix * noise) * amp.getNextSample() * gain);
                if (!amp.isActive()) {
                    clearCurrentNote();
                    return;
                }
            }
        }

    private:
        juce::Random random;
        Oscillator oscillator;
        double increment = 0.0;
        float gain = 1.0f;
        juce::ADSR amp;
    };
}

SampleData Vocoder::builtInVoice() {
    // Formant-synthesised "a e i o u" on a 110 Hz glottal pulse, with breathy "s" between phrases.
    static const SampleData voiceSample = [] {
        SampleData sample;
        sample.name = "Built-in Vowels";
        sample.sampleRate = 48000.0;
        const int length = 96000; // 2 s loop
        sample.audio.setSize(1, length);
        const double vowels[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 },
                                      { 570, 840, 2410 }, { 300, 870, 2240 } };
        const double gains[3] = { 1.0, 0.5, 0.25 };
        std::array<Biquad, 3> formants;
        Biquad hiss;
        hiss.setBandPass(sample.sampleRate, 6000.0, 2.0);
        juce::Random random(4242);
        double phase = 0.0;
        for (int i = 0; i < length; i++) {
            const double t = i / sample.sampleRate;
            const double position = std::fmod(t * 3.0, 5.0); // 3 vowels per second, cycling
            const int vowel = static_cast<int>(position);
            const double blend = position - vowel;
            const auto& a = vowels[vowel];
            const auto& b = vowels[(vowel + 1) % 5];
            if (i % 64 == 0) {
                for (int f = 0; f < 3; f++) {
                    formants[static_cast<size_t>(f)].setBandPass(sample.sampleRate, a[f] + (b[f] - a[f]) * blend * blend, 8.0);
                }
            }
            phase += (110.0 + 3.0 * std::sin(t * 30.0)) / sample.sampleRate;
            phase -= std::floor(phase);
            const auto pulse = static_cast<float>(1.0 - 2.0 * phase); // a saw stands in for the glottal pulse
            float x = 0.0f;
            for (int f = 0; f < 3; f++) {
                x += static_cast<float>(gains[f]) * formants[static_cast<size_t>(f)].process(0, pulse);
            }
            const bool hissing = std::fmod(t, 1.0) > 0.9; // a short "s" at the end of each second
            const float breath = hiss.process(0, random.nextFloat() * 2.0f - 1.0f) * (hissing ? 0.6f : 0.0f);
            sample.audio.setSample(0, i, 0.8f * x + breath);
        }
        sample.audio.applyGain(0.7f / juce::jmax(1.0e-6f, sample.audio.getMagnitude(0, length)));
        return sample;
    }();
    return voiceSample;
}

Vocoder::Vocoder() {
    parameters.add({ "formantShift", "Formant", -12.0f, 12.0f, 0.0f });  // semitones
    parameters.add({ "bandAttack", "Band Attack", 0.001f, 0.05f, 0.004f }); // seconds
    parameters.add({ "bandRelease", "Band Release", 0.01f, 0.5f, 0.05f });
    parameters.add({ "noise", "Noise", 0.0f, 1.0f, 0.1f });
    parameters.add({ "sibilance", "Sibilance", 0.0f, 1.0f, 0.3f });
    parameters.add({ "speed", "Voice Speed", 0.25f, 2.0f, 1.0f });
    parameters.add({ "attack", "Attack", 0.001f, 2.0f, 0.01f });
    parameters.add({ "release", "Release", 0.001f, 4.0f, 0.2f });
    parameters.add({ "level", "Level", 0.0f, 2.0f, 1.0f });
    jassert(parameters.size() == numParameters);
    addVoices<Voice>(8);
    setData({});
}

void Vocoder::prepare(double newSampleRate, int maxBlockSize) {
    sampleRate = newSampleRate;
    VoiceMachine::prepare(newSampleRate, maxBlockSize);
    carrier.setSize(1, juce::jmax(1, maxBlockSize));
    voice.setSize(1, juce::jmax(1, maxBlockSize));
    for (int band = 0; band < numBands; band++) {
        analysis[static_cast<size_t>(band)].setBandPass(sampleRate, bandFrequency(band), bandQ);
    }
    sibilanceFilter.setHighPass(sampleRate, 5000.0);
    lastShift = -999.0f;
    reset();
}

void Vocoder::reset() {
    VoiceMachine::reset();
    for (int band = 0; band < numBands; band++) {
        analysis[static_cast<size_t>(band)].reset();
        synthesis[static_cast<size_t>(band)].reset();
    }
    envelopes = {};
    sibilanceFilter.reset();
    modulatorPosition = 0.0;
    wasSilent = true;
}

void Vocoder::updateBands(float shiftSemitones) {
    const double ratio = std::pow(2.0, shiftSemitones / 12.0);
    for (int band = 0; band < numBands; band++) {
        synthesis[static_cast<size_t>(band)].setBandPass(sampleRate, juce::jmin(sampleRate * 0.45, bandFrequency(band) * ratio), bandQ);
    }
    lastShift = shiftSemitones;
}

void Vocoder::process(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) {
    const int numSamples = juce::jmin(output.getNumSamples(), carrier.getNumSamples());
    const auto* mod = modulator.acquire();

    // The carrier: all voices summed into a mono scratch buffer.
    carrier.clear();
    juce::AudioBuffer<float> carrierView(carrier.getArrayOfWritePointers(), 1, numSamples);
    synth.renderNextBlock(carrierView, midi, 0, numSamples);

    const float shift = parameters.get(formantShift);
    if (!juce::exactlyEqual(shift, lastShift)) {
        updateBands(shift);
    }
    const auto attackCoefficient = static_cast<float>(std::exp(-1.0 / (parameters.get(bandAttack) * sampleRate)));
    const auto releaseCoefficient = static_cast<float>(std::exp(-1.0 / (parameters.get(bandRelease) * sampleRate)));
    const float sibilanceLevel = parameters.get(sibilance);
    const float outputLevel = parameters.get(level) * 4.0f; // band filtering loses a lot of level

    const float* carrierData = carrier.getReadPointer(0);
    const float silence = carrier.getMagnitude(0, 0, numSamples);
    const bool silent = silence < 1.0e-6f;
    if (wasSilent && !silent) {
        modulatorPosition = 0.0; // a note from silence restarts the voice
    }
    wasSilent = silent;
    if (silent || mod == nullptr || mod->sample.isEmpty()) {
        return;
    }

    const auto& modulatorAudio = mod->sample.audio;
    const float* modulatorData = modulatorAudio.getReadPointer(0);
    const int modulatorLength = modulatorAudio.getNumSamples();
    const double modulatorRate = parameters.get(speed) * mod->sample.sampleRate / sampleRate;
    const int rightChannel = output.getNumChannels() > 1 ? 1 : 0;

    for (int i = 0; i < numSamples; i++) {
        const auto index = static_cast<int>(modulatorPosition);
        const auto fraction = static_cast<float>(modulatorPosition - index);
        const float m = modulatorData[index] + fraction * (modulatorData[(index + 1) % modulatorLength] - modulatorData[index]);
        modulatorPosition += modulatorRate;
        if (modulatorPosition >= modulatorLength) {
            modulatorPosition -= modulatorLength;
        }

        float sample = 0.0f;
        for (int band = 0; band < numBands; band++) {
            const float level = std::abs(analysis[static_cast<size_t>(band)].process(0, m));
            auto& envelope = envelopes[static_cast<size_t>(band)];
            envelope = level + (envelope - level) * (level > envelope ? attackCoefficient : releaseCoefficient);
            sample += synthesis[static_cast<size_t>(band)].process(0, carrierData[i]) * envelope;
        }
        // Sibilance: the voice's own high end passes straight through (only while notes play).
        sample += sibilanceLevel * sibilanceFilter.process(0, m) * juce::jmin(1.0f, std::abs(carrierData[i]) * 4.0f);

        const float out = sample * outputLevel;
        output.addSample(0, i, out);
        if (rightChannel != 0) {
            output.addSample(rightChannel, i, out);
        }
    }
}

juce::ValueTree Vocoder::getData() const {
    juce::ValueTree tree(dataType);
    const auto* current = modulator.get();
    if (current != nullptr && !current->builtIn) {
        tree.appendChild(current->sample.toTree(), nullptr);
    }
    return tree;
}

juce::Result Vocoder::setData(const juce::ValueTree& data) {
    auto next = std::make_unique<Modulator>();
    const auto sampleTree = data.getChildWithName("Sample");
    if (sampleTree.isValid()) {
        if (const auto loaded = SampleData::fromTree(sampleTree, next->sample); loaded.failed()) {
            return loaded;
        }
        // The modulator is analysed in mono.
        if (next->sample.audio.getNumChannels() > 1) {
            next->sample.audio.addFrom(0, 0, next->sample.audio, 1, 0, next->sample.audio.getNumSamples());
            next->sample.audio.applyGain(0, 0, next->sample.audio.getNumSamples(), 0.5f);
            next->sample.audio.setSize(1, next->sample.audio.getNumSamples(), true);
        }
        next->builtIn = false;
    } else {
        next->sample = builtInVoice();
    }
    modulator.set(std::move(next));
    return juce::Result::ok();
}
