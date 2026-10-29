#pragma once

namespace rb
{

enum class InstrumentType
{
    analog = 0, // subtractive synth
    wave,       // wavetable synth
    drumKit,    // 16 pads, each a Pad Sampler
    sampler     // melodic sampler
};
constexpr int numInstrumentTypes = 4;

enum class EffectType
{
    none = 0,
    autoFilter,
    eq,
    chorus,
    delay,
    dynamics,
    phaser,
    crusher,
    reverb,
    saturator
};
constexpr int numEffectTypes = 10;

juce::String toTypeId (InstrumentType);
juce::String toTypeId (EffectType);
InstrumentType instrumentTypeFromId (const juce::String&, InstrumentType fallback = InstrumentType::analog);
EffectType effectTypeFromId (const juce::String&, EffectType fallback = EffectType::none);

enum class ParamUnit
{
    none,
    percent,  // value 0..1 shown as 0..100 %
    decibels,
    hertz,
    seconds,  // shown as ms below one second
    semitones,
    cents,
    ratio,    // "4.0:1"
    choice,   // integer index into choices
    toggle    // 0 / 1
};

// One automatable parameter of a device. Values are stored in "real" units (Hz, dB, ...).
struct ParamSpec
{
    juce::String id;
    juce::String name;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float defaultValue = 0.0f;
    ParamUnit unit = ParamUnit::none;
    float skewCentre = 0.0f;    // value shown at the middle of a knob; 0 = linear
    juce::StringArray choices;  // for ParamUnit::choice
    juce::String group;         // UI tab / page this parameter lives on

    static ParamSpec continuous (juce::String id, juce::String name, float min, float max, float def,
                                 ParamUnit unit = ParamUnit::none, float skewCentre = 0.0f,
                                 juce::String group = {});
    static ParamSpec choice (juce::String id, juce::String name, juce::StringArray choices,
                             int def = 0, juce::String group = {});
    static ParamSpec toggle (juce::String id, juce::String name, bool def, juce::String group = {});

    bool isDiscrete() const { return unit == ParamUnit::choice || unit == ParamUnit::toggle; }

    float toNormalised (float value) const;
    float fromNormalised (float normalised) const;
    float clamp (float value) const;
    juce::String format (float value) const;
};

struct DeviceSpec
{
    juce::String typeId;
    juce::String displayName;
    std::vector<ParamSpec> params;
    juce::StringArray groups; // display order of ParamSpec::group values

    int indexOf (const juce::String& paramId) const;
    std::vector<float> defaults() const;
};

// Maximum number of parameters any one device may declare. Sized so the audio thread can use
// fixed arrays when applying automation.
constexpr int maxDeviceParams = 64;

} // namespace rb
