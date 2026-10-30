#pragma once

#include "VoiceMachine.h"
#include "../SampleData.h"
#include "../SwappableResource.h"

#include <vector>

// Multi-sample player (the PCMSynth slot in the spec). The samples are the machine's data: zones,
// each a sample with a root note, a key range, and an optional loop. A synthesised electric-piano
// tone is loaded by default so the machine plays out of the box. Resonant low-pass filter and amp
// envelope per voice. 8 voices.
class Sampler : public VoiceMachine {
public:
    static constexpr const char* typeName = "Sampler";
    static constexpr int maxZones = 32;

    enum ParameterIndex { tune, cutoff, resonance, attack, decay, sustain, release, level, numParameters };

    struct Zone {
        SampleData sample;
        int rootNote = 60;
        int lowKey = 0;
        int highKey = 127;
        bool loop = false;
        double loopStart = 0.0; // fractions of the sample length
        double loopEnd = 1.0;
        juce::String builtIn;   // non-empty for a built-in sample: saved by name, not as audio
    };

    struct Zones {
        std::vector<Zone> zones;
        int zoneFor(int note) const;
    };

    Sampler();
    juce::String getTypeName() const override { return typeName; }
    void process(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) override;

        // <Sampler><Zone root="60" low="0" high="127" loop="0" loopStart="0" loopEnd="1"><Sample .../></Zone>...</Sampler>
    // (a built-in sample is saved as builtIn="keys" instead of a <Sample>)
    juce::ValueTree getData() const override;
    juce::Result setData(const juce::ValueTree& data) override;
    void collectGarbage() override { zones.collect(); }

    static juce::ValueTree dataFor(const std::vector<Zone>& zones);
    static Zone defaultZone();

private:
    SwappableResource<Zones> zones;
    const Zones* blockZones = nullptr;
};
