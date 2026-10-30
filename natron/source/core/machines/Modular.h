#pragma once

#include "VoiceMachine.h"

// Semi-modular synth (the Modular slot in the spec): fixed modules — two oscillators, noise, a
// filter, two envelopes, two LFOs — and six patch cables, each routing a source to a destination
// with an amount. Envelope 1 always drives the amp. 8 voices.
class Modular : public VoiceMachine {
public:
    static constexpr const char* typeName = "Modular";
    static constexpr int numPatches = 6;

    enum Source { none, envelope1, envelope2, lfo1, lfo2, velocity, keyTrack, random, numSources };
    enum Destination {
        noDestination, osc1Pitch, osc2Pitch, bothPitch, oscMixDestination, cutoffDestination,
        resonanceDestination, ampDestination, lfo1RateDestination, noiseDestination, numDestinations
    };

    enum ParameterIndex {
        osc1Shape, osc1Semitones, osc2Shape, osc2Semitones, osc2Fine, oscMix, noise,
        filterType, cutoff, resonance,
        env1Attack, env1Decay, env1Sustain, env1Release,
        env2Attack, env2Decay, env2Sustain, env2Release,
        lfo1Rate, lfo1Shape, lfo2Rate, lfo2Shape,
        patch1Source, // then destination and amount, for each of the six patches
        level = patch1Source + numPatches * 3,
        numParameters
    };

    static int patchParameter(int patch, int field) { return patch1Source + patch * 3 + field; } // 0 src, 1 dst, 2 amount

    Modular();
    juce::String getTypeName() const override { return typeName; }
};
