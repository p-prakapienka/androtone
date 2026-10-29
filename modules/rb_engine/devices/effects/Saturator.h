#pragma once

namespace rb
{

// Saturator effect. STUB: pass-through placeholder until the real implementation lands.
class Saturator : public EffectProcessor
{
public:
    static DeviceSpec createSpec();

    void prepare (double, int) override {}
    void reset() override {}
    void setParameters (const float*, int) override {}
    void process (juce::AudioBuffer<float>&, int) override {}
};

} // namespace rb
