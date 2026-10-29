#pragma once

namespace rb
{

// Parameter values by ParamSpec id; anything missing takes the spec default.
using ParamValues = std::map<juce::String, float>;

struct EffectPreset
{
    juce::String name;
    EffectType type = EffectType::none;
    ParamValues values;
};

struct PadPreset
{
    juce::String name;
    juce::String sampleRef; // usually factory:<id>
    ParamValues values;
};

// Preset categories, in browser order (DEV-3).
const juce::StringArray& getPresetCategories(); // Drums, Bass, Keys, Lead, Pad, Pluck, Mallets, Sampler, FX

struct TrackPreset
{
    juce::String name;
    juce::String category;
    InstrumentType type = InstrumentType::analog;
    ParamValues values;             // instrument parameters
    juce::StringArray macros;       // Analog/Wave: up to 8 param ids shown first (DEV-11)
    std::array<EffectPreset, 2> effects;
    std::array<PadPreset, numDrumPads> pads; // Drum Kit
    juce::String sampleRef;         // Sampler
    bool isUser = false;
};

// Factory content. Each device family contributes its own presets (see presets/*.cpp).
const std::vector<TrackPreset>& getFactoryPresets();
const std::vector<EffectPreset>& getFactoryEffectPresets();

void addAnalogPresets (std::vector<TrackPreset>&);
void addWavePresets (std::vector<TrackPreset>&);
void addDrumKitPresets (std::vector<TrackPreset>&);
void addSamplerPresets (std::vector<TrackPreset>&);
void addEffectPresets (std::vector<EffectPreset>&);

// Returns a factory preset by name, or nullptr.
const TrackPreset* findFactoryPreset (const juce::String& name);

// Default effect for a slot when a preset leaves it empty.
EffectPreset defaultEffectPreset (EffectType);

// Applies a preset to a track: instrument, values, effects, pads/sample, preset name.
// Clips, mixer state and performance options are kept.
void applyPreset (Track&, const TrackPreset&);
void applyEffectPreset (Track&, int effectSlot, const EffectPreset&);

// Captures a track's current sound as a preset (for saving user presets, DEV-4).
TrackPreset presetFromTrack (const Track&, const juce::String& name);

// User presets: one JSON file per preset in a folder.
juce::var presetToVar (const TrackPreset&);
TrackPreset presetFromVar (const juce::var&);
std::vector<TrackPreset> loadUserPresets (const juce::File& folder);
bool saveUserPreset (const juce::File& folder, const TrackPreset&);

} // namespace rb
