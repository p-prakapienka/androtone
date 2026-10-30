#pragma once

#include "VoiceMachine.h"
#include "../SampleData.h"
#include "../SwappableResource.h"
#include "../dsp/Biquad.h"

#include <array>

// Channel vocoder (the Vocoder slot in the spec). The notes play a saw + noise carrier (8 voices);
// a looping modulator voice is analysed by a 16-band filterbank whose envelopes shape the carrier.
// The modulator is the machine's data: a synthesised vowel phrase by default, or an imported
// sample. It restarts whenever a note starts from silence.
class Vocoder : public VoiceMachine {
public:
    static constexpr const char* typeName = "Vocoder";
    static constexpr int numBands = 16;

    enum ParameterIndex { formantShift, bandAttack, bandRelease, noise, sibilance, speed, attack, release, level, numParameters };

    struct Modulator {
        SampleData sample;
        bool builtIn = true;
    };

    Vocoder();
    juce::String getTypeName() const override { return typeName; }
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) override;
    void reset() override;

    // <Vocoder/> (built-in voice) or <Vocoder><Sample .../></Vocoder>
    juce::ValueTree getData() const override;
    juce::Result setData(const juce::ValueTree& data) override;
    void collectGarbage() override { modulator.collect(); }

    static SampleData builtInVoice();

private:
    SwappableResource<Modulator> modulator;
    double sampleRate = 48000.0;
    juce::AudioBuffer<float> carrier;   // mono scratch, sized in prepare()
    juce::AudioBuffer<float> voice;     // mono scratch
    std::array<Biquad, numBands> analysis;
    std::array<Biquad, numBands> synthesis;
    std::array<float, numBands> envelopes {};
    Biquad sibilanceFilter;
    double modulatorPosition = 0.0;
    float lastShift = -999.0f;
    bool wasSilent = true;

    void updateBands(float shiftSemitones);
};
