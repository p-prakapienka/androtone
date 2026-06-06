#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SamplePlayerSound.h"

// Voice for SamplePlayerSound. It reuses JUCE's SamplerVoice for playback but
// makes it a true one-shot: note-off is ignored, so the sample always plays to
// its end (SamplerVoice::renderNextBlock clears the note once the read position
// passes the end of the sample). A hard stop (voice steal) is still honoured so
// the voice can be reused.
class SamplePlayerVoice : public juce::SamplerVoice {
public:
    bool canPlaySound(juce::SynthesiserSound* sound) override {
        return dynamic_cast<SamplePlayerSound*>(sound) != nullptr;
    }

    void startNote(int midiNote, float velocity, juce::SynthesiserSound* sound, int currentPitchWheelPosition) override {
        // SamplerVoice uses velocity directly as its playback gain, so pre-scale it
        // by the same 0.15 factor SawVoice/SineVoice apply to keep levels in line.
        juce::SamplerVoice::startNote(midiNote, velocity * 0.15f, sound, currentPitchWheelPosition);
    }

    void stopNote(float velocity, bool allowTailOff) override {
        if (!allowTailOff) {
            juce::SamplerVoice::stopNote(velocity, false);
        }
    }
};
