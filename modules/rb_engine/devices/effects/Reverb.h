#pragma once

namespace rb
{

// Reverb effect. STUB: pass-through placeholder until the real implementation lands.
class Reverb : public EffectProcessor
{
public:
    static DeviceSpec createSpec();

    void prepare (double, int) override {}
    void reset() override {}
    void setParameters (const float*, int) override {}
    void process (juce::AudioBuffer<float>&, int) override {}
};

} // namespace rb
