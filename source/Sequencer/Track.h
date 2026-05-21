#pragma once

#include "Pattern.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

class Track {
public:
    Track() = default;

    void setChannel(int newChannel) {
        channel = newChannel;
    }

    template<typename Container>
    void setPattern(const Container& newPattern) {
        pattern.assign(newPattern.begin(), newPattern.end());
        if (currentStep >= getNumSteps()) {
            currentStep = 0;
        }
    }

    int getNumSteps() const {
        return static_cast<int>(pattern.size());
    }

    void reset() {
        currentStep = 0;
        sampleCounter = 0.0;
        samplesPerStep = 0.0;
        hasHeldNote = false;
        heldNoteNumber = 0;
    }

    void releaseHeldNote(juce::MidiBuffer& midi, int samplePosition) {
        if (hasHeldNote) {
            midi.addEvent(juce::MidiMessage::noteOff(channel, heldNoteNumber), samplePosition);
            hasHeldNote = false;
        }
    }

    void processBlock(juce::MidiBuffer& midi, int numSamples, double bpm, double sampleRate) {
        if (pattern.empty()) {
            return;
        }

        updateSamplesPerStep(bpm, sampleRate);

        for (int sample = 0; sample < numSamples; sample++) {
            if (sampleCounter >= samplesPerStep) {
                sampleCounter = 0;

                if (hasHeldNote) {
                    midi.addEvent(juce::MidiMessage::noteOff(channel, heldNoteNumber), sample);
                    hasHeldNote = false;
                }

                currentStep = (currentStep + 1) % getNumSteps();
                const Note& nextNote = pattern[currentStep];
                if (nextNote.velocity > 0) {
                    midi.addEvent(
                        juce::MidiMessage::noteOn(channel, nextNote.number, static_cast<juce::uint8>(nextNote.velocity)),
                        sample
                    );
                    heldNoteNumber = nextNote.number;
                    hasHeldNote = true;
                }

                updateSamplesPerStep(bpm, sampleRate);
            }
            ++sampleCounter;
        }
    }

private:
    int channel = 1;
    std::vector<Note> pattern;
    int currentStep = 0;
    double sampleCounter = 0.0;
    double samplesPerStep = 0.0;
    bool hasHeldNote = false;
    int heldNoteNumber = 0;

    void updateSamplesPerStep(double bpm, double sampleRate) {
        const double beatsPerSecond = bpm / 60.0;
        const double stepLength = pattern[currentStep].length;
        const double secondsPerStep = stepLength / beatsPerSecond;
        samplesPerStep = secondsPerStep * sampleRate;
    }
};
