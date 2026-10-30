#pragma once

#include "VoiceMachine.h"

// Tonewheel-style drawbar organ (the Organ slot in the spec): nine sine drawbars, percussion on the
// 2nd or 3rd harmonic, key click, vibrato, and a rotary speaker (stereo amplitude/pan rotation).
// 16 voices.
class Organ : public VoiceMachine {
public:
    static constexpr const char* typeName = "Organ";
    static constexpr int numDrawbars = 9;
    // Harmonic of each drawbar, relative to the played note (16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1').
    static constexpr double drawbarRatios[numDrawbars] = { 0.5, 1.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0 };

    enum ParameterIndex {
        drawbar1 = 0, // ...drawbar9 = 8
        percussion = numDrawbars, percussionHarmonic, percussionDecay, percussionLevel,
        click, vibratoDepth, vibratoRate, rotarySpeed, rotaryDepth, level,
        numParameters
    };

    Organ();
    juce::String getTypeName() const override { return typeName; }
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) override;

private:
    double sampleRate = 48000.0;
    double rotorPhase = 0.0;
    double rotorRate = 0.0; // Hz, eases towards the selected speed like a real rotor
    double vibratoPhase = 0.0;
    double vibratoSemitones = 0.0; // read by the voices
};
