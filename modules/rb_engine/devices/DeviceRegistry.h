#pragma once

namespace rb
{

// Parameter layouts and factories for every device type.
const DeviceSpec& getInstrumentSpec (InstrumentType);
const DeviceSpec& getEffectSpec (EffectType);
const DeviceSpec& getPadSpec(); // per-pad parameters of the Drum Kit (Pad Sampler)

std::unique_ptr<InstrumentProcessor> createInstrument (InstrumentType);
std::unique_ptr<EffectProcessor> createEffect (EffectType); // nullptr for EffectType::none

juce::String getInstrumentDisplayName (InstrumentType);
juce::String getEffectDisplayName (EffectType);

// Tests can substitute instruments (e.g. a probe that records note events).
using InstrumentFactory = std::function<std::unique_ptr<InstrumentProcessor> (InstrumentType)>;
void setInstrumentFactoryOverride (InstrumentFactory);

// Brings a stored value list in line with the spec: missing values get defaults, extra values
// are dropped, everything is clamped.
std::vector<float> conformValues (const DeviceSpec&, std::vector<float> values);

} // namespace rb
