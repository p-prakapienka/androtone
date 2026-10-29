#pragma once

#include "../Machine.h"

#include <vector>

// Two-oscillator subtractive synth (the SubSynth slot in the spec): osc 1 + osc 2 (with semitone
// and fine offset), state-variable filter with its own envelope, amp envelope, and an LFO that can
// move the cutoff and the pitch. 8 voices.
class SubtractiveSynth : public Machine {
public:
    static constexpr const char* typeName = "Subtractive";
    static constexpr int numVoices = 8;

    enum ParameterIndex {
        osc1Shape, osc2Shape, osc2Semitones, osc2Fine, oscMix,
        filterType, cutoff, resonance, filterEnvAmount,
        filterAttack, filterDecay, filterSustain, filterRelease,
        ampAttack, ampDecay, ampSustain, ampRelease,
        lfoRate, lfoToCutoff, lfoToPitch,
        level,
        numParameters
    };

    SubtractiveSynth();

    juce::String getTypeName() const override { return typeName; }
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) override;
    void reset() override;

private:
    juce::Synthesiser synth;
    std::vector<float> lfoValues; // sized in prepare(); the engine never exceeds maxBlockSize
    double lfoPhase = 0.0;
};
