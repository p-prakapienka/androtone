#pragma once

#include "../Machine.h"
#include "../dsp/Oscillator.h"

#include <array>

// Monophonic 303-style bass (the BassLine slot in the spec). Pattern conventions, as on MIDI 303s:
//  - accent: note velocity >= accentThreshold — louder, with a stronger filter sweep;
//  - slide: a note that starts while the previous one is still held glides to the new pitch
//    without retriggering the envelopes (make the earlier note overlap the next one).
class AcidBass : public Machine {
public:
    static constexpr const char* typeName = "Acid";
    static constexpr float accentThreshold = 0.9f;

    enum ParameterIndex { shape, tune, cutoff, resonance, envMod, decay, accent, slide, level, numParameters };

    AcidBass();

    juce::String getTypeName() const override { return typeName; }
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) override;
    void reset() override;

private:
    double sampleRate = 48000.0;

    // Held notes, most recent last (for legato fallback on release).
    std::array<int, 16> held {};
    int numHeld = 0;

    double pitch = 45.0;        // current, in semitones (glides towards target)
    double incrementPitch = -1.0e9; // the pitch `increment` was computed for
    double increment = 0.0;         // oscillator phase increment for incrementPitch
    double targetPitch = 45.0;
    bool sliding = false;
    bool gate = false;
    bool accented = false;
    float amp = 0.0f;           // follows the gate
    float filterEnvelope = 0.0f;
    float accentEnvelope = 0.0f;

    Oscillator oscillator;
    std::array<float, 4> ladder {};
    float cutoffCoefficient = 0.0f;
    int controlCounter = 0;

    void noteOn(int note, float velocity);
    void noteOff(int note);
    void render(juce::AudioBuffer<float>& output, int start, int end);
};
