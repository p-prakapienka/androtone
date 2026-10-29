#pragma once

namespace rb
{

// EQ effect. STUB: pass-through placeholder until the real implementation lands.
class ChannelEq : public EffectProcessor
{
public:
    static DeviceSpec createSpec();

    void prepare (double, int) override {}
    void reset() override {}
    void setParameters (const float*, int) override {}
    void process (juce::AudioBuffer<float>&, int) override {}
};

} // namespace rb
