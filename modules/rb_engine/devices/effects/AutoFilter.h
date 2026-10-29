#pragma once

namespace rb
{

// Auto Filter effect. STUB: pass-through placeholder until the real implementation lands.
class AutoFilter : public EffectProcessor
{
public:
    static DeviceSpec createSpec();

    void prepare (double, int) override {}
    void reset() override {}
    void setParameters (const float*, int) override {}
    void process (juce::AudioBuffer<float>&, int) override {}
};

} // namespace rb
