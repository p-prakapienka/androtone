#pragma once

namespace rb
{

// Sets are stored as JSON (set.json inside the set's folder). Parameters are keyed by id so
// adding parameters to a device later keeps old sets loadable.
juce::var setToVar (const Set&);
Set setFromVar (const juce::var&);

juce::var clipToVar (const Clip&, const Track&);
ClipPtr clipFromVar (const juce::var&, const Track&);

juce::var valuesToVar (const DeviceSpec&, const std::vector<float>&);
std::vector<float> valuesFromVar (const DeviceSpec&, const juce::var&);

// Spec of the device an automation target refers to on this track.
const DeviceSpec* specForTarget (const Track&, const ParamTarget&);

} // namespace rb
