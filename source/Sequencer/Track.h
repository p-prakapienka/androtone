#pragma once

#include "Pattern.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

class Track {
public:
    Track() {
        notesPlaying.reserve(1);
    }

    void processBlock(juce::MidiBuffer& midi, int numSamples, double bpm, double sampleRate) {
        if (pattern.empty()) {
            return;
        }

        if (currentStep >= 0) {
            updateSamplesPerStep(bpm, sampleRate);
        }

        for (int sample = 0; sample < numSamples; sample++) {
            if (sampleCounter >= samplesPerStep) {
                sampleCounter = 0;

                //TODO need to implement polyphonic notes support here, ex. when multiple notes of different length
                if (!notesPlaying.empty()) {
                    const Note prevNote = notesPlaying.back();
                    notesPlaying.pop_back();
                    midi.addEvent(juce::MidiMessage::noteOff(channel, prevNote.number), sample);
                }

                currentStep = (currentStep + 1) % getNumSteps();
                const Note& nextNote = pattern[currentStep];
                if (nextNote.velocity > 0) {
                    midi.addEvent(
                        juce::MidiMessage::noteOn(channel, nextNote.number, static_cast<juce::uint8>(nextNote.velocity)),
                        sample
                    );
                    notesPlaying.push_back(nextNote);
                }

                updateSamplesPerStep(bpm, sampleRate);
            }
            sampleCounter++;
        }
    }

    // TODO: add pause support — drain notes but preserve currentStep/sampleCounter so playback can resume
    void stop(juce::MidiBuffer& midi, int samplePosition) {
        for (auto& note : notesPlaying) {
            midi.addEvent(juce::MidiMessage::noteOff(channel, note.number), samplePosition);
        }
        notesPlaying.clear();
        currentStep = -1;
        sampleCounter = 0.0;
    }

    void setChannel(int newChannel) {
        channel = newChannel;
    }

    template<typename Container>
    void setPattern(const Container& newPattern) {
        pattern.assign(newPattern.begin(), newPattern.end());
        if (currentStep >= getNumSteps()) {
            currentStep = -1;
        }
    }

    int getNumSteps() const {
        return static_cast<int>(pattern.size());
    }

private:
    int channel = 1;
    std::vector<Note> pattern;
    int currentStep = -1;
    double sampleCounter = 0.0;
    double samplesPerStep = 0.0;
    std::vector<Note> notesPlaying;

    void updateSamplesPerStep(double bpm, double sampleRate) {
        const double beatsPerSecond = bpm / 60.0;
        const double stepLength = pattern[currentStep].length;
        const double secondsPerStep = stepLength / beatsPerSecond;
        samplesPerStep = secondsPerStep * sampleRate;
    }
};
