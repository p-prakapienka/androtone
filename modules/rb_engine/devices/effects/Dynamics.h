#pragma once

namespace rb
{

// Dynamics effect. STUB: pass-through placeholder until the real implementation lands.
class Dynamics : public EffectProcessor
{
public:
    static DeviceSpec createSpec();

    void prepare (double, int) override {}
    void reset() override {}
    void setParameters (const float*, int) override {}
    void process (juce::AudioBuffer<float>&, int) override {}
};

} // namespace rb
