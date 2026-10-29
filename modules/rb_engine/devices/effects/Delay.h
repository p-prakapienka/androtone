#pragma once

namespace rb
{

// Delay effect. STUB: pass-through placeholder until the real implementation lands.
class Delay : public EffectProcessor
{
public:
    static DeviceSpec createSpec();

    void prepare (double, int) override {}
    void reset() override {}
    void setParameters (const float*, int) override {}
    void process (juce::AudioBuffer<float>&, int) override {}
};

} // namespace rb
