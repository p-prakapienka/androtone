#pragma once

namespace rb
{

// Wave: a wavetable polysynth (DEV-15). Two morphing wavetable oscillators with unison, sub
// and noise, a resonant multimode filter, amp and mod envelopes, an LFO that can sweep the
// wavetable position, and mono/legato modes with glide. The tables are generated in code
// (WaveTables.h) and shared by every instance.
class WaveSynth : public InstrumentProcessor
{
public:
    WaveSynth();
    ~WaveSynth() override;

    static DeviceSpec createSpec();

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void setParameters (const float* values, int numValues) override;
    void process (juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>& send,
                  const NoteEvent* events, int numEvents, int numSamples) override;
    void allNotesOff() override;
    void setPitchBend (float semitones) override;
    void setPressure (int note, float pressure) override;
    void setTempo (double bpm) override;

    struct Impl;

private:
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveSynth)
};

} // namespace rb
