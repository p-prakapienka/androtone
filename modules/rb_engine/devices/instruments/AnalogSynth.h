#pragma once

namespace rb
{

// Analog: a two-oscillator subtractive polysynth (DEV-11). Morphing sine/triangle/saw/pulse
// oscillators with sub, noise and FM, a resonant multimode filter with drive, amp and mod
// envelopes, an LFO, mono/legato modes with glide, and per-voice analog drift.
class AnalogSynth : public InstrumentProcessor
{
public:
    AnalogSynth();
    ~AnalogSynth() override;

    static DeviceSpec createSpec();

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void setParameters (const float* values, int numValues) override;
    void process (juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>& send,
                  const NoteEvent* events, int numEvents, int numSamples) override;
    void allNotesOff() override;

    struct Impl;

private:
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalogSynth)
};

} // namespace rb
