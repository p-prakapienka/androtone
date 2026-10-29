#pragma once

namespace rb
{

// Sample tab (SMP-2..SMP-6): waveform of the selected pad's / sampler's sample, load,
// record from the microphone or resample the output, sample options.
// STUB: waveform only until the full view lands.
class SampleView : public juce::Component,
                   private Studio::Listener
{
public:
    SampleView (Studio&, Navigator&, int trackId);
    ~SampleView() override;

    void paint (juce::Graphics&) override;

private:
    void studioChanged() override { repaint(); }

    Studio& studio;
    Navigator& nav;
    int trackId;
};

} // namespace rb
