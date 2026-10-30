#include "Sampler.h"

#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace {
    using P = Sampler::ParameterIndex;
    const juce::Identifier dataType { "Sampler" };
    const juce::Identifier zoneType { "Zone" };

    class Voice : public MachineVoice {
    public:
        Voice(const ParameterSet& parametersToUse, int) :
            MachineVoice(parametersToUse) {}

        void setZones(const Sampler::Zones* const* source) { zones = source; }

        void prepare(double sampleRate, int) override {
            filter.prepare({ sampleRate, 64, 2 });
            amp.setSampleRate(sampleRate);
        }

        void startNote(int midiNote, float velocity, juce::SynthesiserSound*, int) override {
            const auto* current = zones != nullptr ? *zones : nullptr;
            zoneIndex = current != nullptr ? current->zoneFor(midiNote) : -1;
            if (zoneIndex < 0) {
                clearCurrentNote();
                return;
            }
            note = midiNote;
            gain = velocity;
            position = 0.0;
            filter.reset();
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
            if (!isVoiceActive()) {
                return;
            }
            // Zones may have been replaced since the note started: look the zone up again.
            const auto* current = zones != nullptr ? *zones : nullptr;
            if (current == nullptr || zoneIndex >= static_cast<int>(current->zones.size())) {
                clearCurrentNote();
                return;
            }
            const auto& zone = current->zones[static_cast<size_t>(zoneIndex)];
            const auto& audio = zone.sample.audio;
            const int length = audio.getNumSamples();
            const double rate = std::pow(2.0, (note - zone.rootNote + get(P::tune)) / 12.0) * zone.sample.sampleRate / getSampleRate();
            // Interpolation reads one sample ahead, so the loop must wrap by the last readable position.
            const double lastReadable = static_cast<double>(length - 1);
            const double loopStart = juce::jmin(zone.loopStart * length, lastReadable - 1.0);
            const double loopEnd = juce::jlimit(loopStart + 1.0, lastReadable, zone.loopEnd * length);
            const float* left = audio.getReadPointer(0);
            const float* right = audio.getReadPointer(audio.getNumChannels() > 1 ? 1 : 0);
            const float level = get(P::level) * gain;

            filter.setCutoffFrequency(juce::jlimit(20.0f, static_cast<float>(getSampleRate() * 0.45), get(P::cutoff)));
            filter.setResonance(juce::jmap(get(P::resonance), 0.5f, 8.0f));
            const int rightChannel = output.getNumChannels() > 1 ? 1 : 0;

            for (int i = startSample; i < startSample + numSamples; i++) {
                if (zone.loop && position >= loopEnd) {
                    position -= loopEnd - loopStart;
                }
                const auto index = static_cast<int>(position);
                if (index >= length - 1) {
                    clearCurrentNote();
                    return;
                }
                const auto fraction = static_cast<float>(position - index);
                const float l = left[index] + fraction * (left[index + 1] - left[index]);
                const float r = right[index] + fraction * (right[index + 1] - right[index]);
                position += rate;

                const float envelope = amp.getNextSample() * level;
                output.addSample(0, i, filter.processSample(0, l) * envelope);
                if (rightChannel != 0) {
                    output.addSample(rightChannel, i, filter.processSample(1, r) * envelope);
                }
                if (!amp.isActive()) {
                    clearCurrentNote();
                    return;
                }
            }
        }

    private:
        const Sampler::Zones* const* zones = nullptr;
        int zoneIndex = -1;
        int note = 60;
        double position = 0.0;
        float gain = 1.0f;
        juce::dsp::StateVariableTPTFilter<float> filter;
        juce::ADSR amp;
    };
}

int Sampler::Zones::zoneFor(int note) const {
    for (size_t i = 0; i < zones.size(); i++) {
        if (note >= zones[i].lowKey && note <= zones[i].highKey && !zones[i].sample.isEmpty()) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

Sampler::Zone Sampler::defaultZone() {
    // A soft electric-piano-like tone at middle C: a few decaying partials with a little bell.
    Zone zone;
    zone.sample.name = "Built-in Keys";
    zone.sample.sampleRate = 48000.0;
    const int length = 96000;
    zone.sample.audio.setSize(1, length);
    const double f = juce::MidiMessage::getMidiNoteInHertz(60);
    const double partials[][3] = { { 1.0, 1.0, 1.4 }, { 2.0, 0.45, 0.8 }, { 3.0, 0.2, 0.5 }, { 4.0, 0.12, 0.35 }, { 7.0, 0.05, 0.15 } };
    for (int i = 0; i < length; i++) {
        const double t = i / zone.sample.sampleRate;
        double x = 0.0;
        for (const auto& p : partials) {
            x += p[1] * std::exp(-t / p[2]) * std::sin(juce::MathConstants<double>::twoPi * f * p[0] * t);
        }
        const double attack = juce::jmin(1.0, t / 0.003);
        zone.sample.audio.setSample(0, i, static_cast<float>(0.5 * attack * x));
    }
    zone.rootNote = 60;
    zone.builtIn = "keys";
    return zone;
}

Sampler::Sampler() {
    parameters.add({ "tune", "Tune", -24.0f, 24.0f, 0.0f });
    parameters.add({ "cutoff", "Cutoff", 20.0f, 20000.0f, 20000.0f });
    parameters.add({ "resonance", "Resonance", 0.0f, 1.0f, 0.0f });
    parameters.add({ "attack", "Attack", 0.001f, 4.0f, 0.002f });
    parameters.add({ "decay", "Decay", 0.001f, 4.0f, 1.0f });
    parameters.add({ "sustain", "Sustain", 0.0f, 1.0f, 1.0f });
    parameters.add({ "release", "Release", 0.001f, 4.0f, 0.3f });
    parameters.add({ "level", "Level", 0.0f, 1.0f, 0.7f });
    jassert(parameters.size() == numParameters);

    addVoices<Voice>(8);
    for (int i = 0; i < synth.getNumVoices(); i++) {
        static_cast<Voice*>(synth.getVoice(i))->setZones(&blockZones);
    }
    auto initial = std::make_unique<Zones>();
    initial->zones.push_back(defaultZone());
    zones.set(std::move(initial));
}

void Sampler::process(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) {
    blockZones = zones.acquire();
    VoiceMachine::process(output, midi);
}

juce::ValueTree Sampler::dataFor(const std::vector<Zone>& zoneList) {
    juce::ValueTree tree(dataType);
    for (const auto& zone : zoneList) {
        juce::ValueTree zoneTree(zoneType);
        zoneTree.setProperty("root", zone.rootNote, nullptr);
        zoneTree.setProperty("low", zone.lowKey, nullptr);
        zoneTree.setProperty("high", zone.highKey, nullptr);
        zoneTree.setProperty("loop", zone.loop, nullptr);
        zoneTree.setProperty("loopStart", zone.loopStart, nullptr);
        zoneTree.setProperty("loopEnd", zone.loopEnd, nullptr);
        if (zone.builtIn.isNotEmpty()) {
            zoneTree.setProperty("builtIn", zone.builtIn, nullptr);
        } else {
            zoneTree.appendChild(zone.sample.toTree(), nullptr);
        }
        tree.appendChild(zoneTree, nullptr);
    }
    return tree;
}

juce::ValueTree Sampler::getData() const {
    const auto* current = zones.get();
    return dataFor(current != nullptr ? current->zones : std::vector<Zone> {});
}

juce::Result Sampler::setData(const juce::ValueTree& data) {
    auto next = std::make_unique<Zones>();
    if (!data.hasType(dataType)) {
        next->zones.push_back(defaultZone());
    } else {
        for (const auto& zoneTree : data) {
            if (!zoneTree.hasType(zoneType)) {
                continue;
            }
            if (static_cast<int>(next->zones.size()) >= maxZones) {
                return juce::Result::fail("Too many sample zones (max " + juce::String(maxZones) + ")");
            }
            Zone zone;
            if (zoneTree.getProperty("builtIn").toString() == "keys") {
                zone = defaultZone();
            } else if (const auto loaded = SampleData::fromTree(zoneTree.getChildWithName("Sample"), zone.sample); loaded.failed()) {
                return loaded;
            }
            zone.rootNote = juce::jlimit(0, 127, static_cast<int>(zoneTree.getProperty("root", 60)));
            zone.lowKey = juce::jlimit(0, 127, static_cast<int>(zoneTree.getProperty("low", 0)));
            zone.highKey = juce::jlimit(zone.lowKey, 127, static_cast<int>(zoneTree.getProperty("high", 127)));
            zone.loop = zoneTree.getProperty("loop", false);
            zone.loopStart = juce::jlimit(0.0, 1.0, static_cast<double>(zoneTree.getProperty("loopStart", 0.0)));
            zone.loopEnd = juce::jlimit(zone.loopStart, 1.0, static_cast<double>(zoneTree.getProperty("loopEnd", 1.0)));
            next->zones.push_back(std::move(zone));
        }
    }
    zones.set(std::move(next));
    return juce::Result::ok();
}
