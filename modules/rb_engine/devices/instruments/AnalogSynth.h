#pragma once

namespace rb
{

// Analog instrument. STUB: silent placeholder until the real implementation lands.
class AnalogSynth : public InstrumentProcessor
{
public:
    static DeviceSpec createSpec();
    void prepare (double, int) override {}
    void reset() override {}
    void setParameters (const float*, int) override {}
    void process (juce::AudioBuffer<float>&, juce::AudioBuffer<float>&, const NoteEvent*, int, int) override {}
    void allNotesOff() override {}
};

} // namespace rb
