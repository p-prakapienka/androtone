#pragma once

namespace rb
{

// Drum Kit instrument. STUB: silent placeholder until the real implementation lands.
class DrumKit : public InstrumentProcessor
{
public:
    static DeviceSpec createSpec();
    // Per-pad parameters (Pad Sampler, DEV-12/13).
    static DeviceSpec createPadSpec();

    void prepare (double, int) override {}
    void reset() override {}
    void setParameters (const float*, int) override {}
    void process (juce::AudioBuffer<float>&, juce::AudioBuffer<float>&, const NoteEvent*, int, int) override {}
    void allNotesOff() override {}
};

} // namespace rb
