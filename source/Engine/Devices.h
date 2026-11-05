#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <vector>

// Per-track devices: an instrument (what notes sound like) followed by an insert-effect chain.
// Parameters are described by ParamSpec tables so the Device tab can build its controls and the
// set file can store them generically.

enum class InstrumentType { Sampler = 0, Synth = 1, Drums = 2 };

struct ParamSpec {
    const char* id;
    const char* name;
    float min, max, defaultValue;
    const char* unit;
    bool integer = false;
    float skewMidPoint = 0.0f; // > 0: skew the control so this value sits in the middle
};

static constexpr int MAX_DEVICE_PARAMS = 12;

// Instrument parameter indices (the order of the spec tables below).
namespace SynthParam { enum { Wave, Octave, Cutoff, Resonance, FilterEnv, Attack, Decay, Sustain, Release, Level }; }
namespace DrumParam { enum { Tune, DecayScale, Level }; }
namespace SamplerParam { enum { Level, Tune }; }
namespace EffectParam { enum { FilterMode, Cutoff, Resonance, Threshold, Ratio, Makeup }; }

inline const std::vector<ParamSpec>& instrumentSpecs(InstrumentType type) {
    static const std::vector<ParamSpec> synth {
        { "wave", "Wave", 0.0f, 3.0f, 1.0f, "", true },
        { "octave", "Octave", -2.0f, 2.0f, 0.0f, "", true },
        { "cutoff", "Cutoff", 40.0f, 18000.0f, 2000.0f, " Hz", false, 1000.0f },
        { "resonance", "Reso", 0.0f, 0.95f, 0.1f, "" },
        { "filterEnv", "Flt Env", 0.0f, 1.0f, 0.3f, "" },
        { "attack", "Attack", 0.001f, 2.0f, 0.005f, " s", false, 0.1f },
        { "decay", "Decay", 0.01f, 2.0f, 0.3f, " s", false, 0.3f },
        { "sustain", "Sustain", 0.0f, 1.0f, 0.7f, "" },
        { "release", "Release", 0.01f, 4.0f, 0.2f, " s", false, 0.4f },
        { "level", "Level", 0.0f, 1.0f, 0.7f, "" },
    };
    static const std::vector<ParamSpec> drums {
        { "tune", "Tune", 0.5f, 2.0f, 1.0f, "x", false, 1.0f },
        { "decay", "Decay", 0.25f, 2.0f, 1.0f, "x", false, 1.0f },
        { "level", "Level", 0.0f, 1.0f, 0.8f, "" },
    };
    static const std::vector<ParamSpec> sampler {
        { "level", "Level", 0.0f, 1.0f, 0.7f, "" },
        { "tune", "Tune", -12.0f, 12.0f, 0.0f, " st", true },
    };
    switch (type) {
        case InstrumentType::Synth: return synth;
        case InstrumentType::Drums: return drums;
        case InstrumentType::Sampler: break;
    }
    return sampler;
}

inline const std::vector<ParamSpec>& effectSpecs() {
    static const std::vector<ParamSpec> effects {
        { "filterMode", "Filter", 0.0f, 2.0f, 0.0f, "", true },     // off / low-pass / high-pass
        { "fxCutoff", "Freq", 20.0f, 20000.0f, 1000.0f, " Hz", false, 1000.0f },
        { "fxResonance", "Reso", 0.0f, 0.95f, 0.2f, "" },
        { "threshold", "Thresh", -60.0f, 0.0f, 0.0f, " dB" },        // 0 dB = compressor idle
        { "ratio", "Ratio", 1.0f, 20.0f, 4.0f, ":1", false, 4.0f },
        { "makeup", "Makeup", 0.0f, 24.0f, 0.0f, " dB" },
    };
    return effects;
}

inline const char* waveName(int wave) {
    static const char* names[] { "Sine", "Saw", "Square", "Triangle" };
    return names[juce::jlimit(0, 3, wave)];
}

inline const char* filterModeName(int mode) {
    static const char* names[] { "Off", "Low-pass", "High-pass" };
    return names[juce::jlimit(0, 2, mode)];
}

// Atomic parameter values, written by the UI and read by the audio thread.
struct DeviceParams {
    std::array<std::atomic<float>, MAX_DEVICE_PARAMS> values {};

    float get(int index) const { return values[static_cast<size_t>(index)].load(); }
    void set(int index, float value) { values[static_cast<size_t>(index)].store(value); }

    void resetTo(const std::vector<ParamSpec>& specs) {
        for (size_t i = 0; i < specs.size() && i < values.size(); i++) {
            values[i].store(specs[i].defaultValue);
        }
    }
};

// One track's devices.
struct TrackDevices {
    std::atomic<InstrumentType> instrument { InstrumentType::Synth };
    DeviceParams instrumentParams;
    DeviceParams effectParams;

    TrackDevices() {
        instrumentParams.resetTo(instrumentSpecs(InstrumentType::Synth));
        effectParams.resetTo(effectSpecs());
    }
};
