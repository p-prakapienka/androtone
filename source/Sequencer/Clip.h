#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

struct Note {
    int number;
    double length;
    int velocity;
};

class Clip {
public:
    // A bar is a fixed 16 steps. Clip switching is quantized to bar boundaries, so clips are
    // expected to be at least this many steps — a shorter clip never reaches a bar boundary
    // and so would never hand off to a queued clip.
    static constexpr int stepsPerBar = 16;

    Clip() {
        notesPlaying.reserve(1);
    }

    // Renders up to `numSamples` samples into `midi`, with all event positions written at
    // `sample + sampleOffset` so the caller can chain a second clip into the same buffer.
    // When `stopOnBarLastStep` is true and a bar's last step has just finished (every
    // `stepsPerBar` steps), it emits the trailing noteOff, resets state, and returns the
    // sample index where it stopped — letting the caller swap to a different clip at that
    // exact bar boundary. Otherwise it processes the full block and returns `numSamples`.
    int processBlock(
        juce::MidiBuffer& midi,
        int numSamples,
        int channel,
        double bpm,
        double sampleRate,
        bool stopOnBarLastStep = false,
        int sampleOffset = 0
    ) {
        if (notes.empty()) {
            return numSamples;
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
                    midi.addEvent(juce::MidiMessage::noteOff(channel, prevNote.number), sample + sampleOffset);
                }

                const int prevStep = currentStep;
                currentStep = (currentStep + 1) % getNumSteps();

                if (stopOnBarLastStep && prevStep >= 0 && (prevStep + 1) % stepsPerBar == 0) {
                    currentStep = -1;
                    sampleCounter = 0.0;
                    samplesPerStep = 0.0; // so the next entry fires step 0 on its first sample
                    return sample;
                }

                const Note& nextNote = notes[currentStep];
                if (nextNote.velocity > 0) {
                    midi.addEvent(
                        juce::MidiMessage::noteOn(channel, nextNote.number, static_cast<juce::uint8>(nextNote.velocity)),
                        sample + sampleOffset
                    );
                    notesPlaying.push_back(nextNote);
                }

                updateSamplesPerStep(bpm, sampleRate);
            }
            sampleCounter++;
        }
        return numSamples;
    }

    // TODO: add pause support — drain notes but preserve currentStep/sampleCounter so playback can resume
    void stop(juce::MidiBuffer& midi, int samplePosition, int channel) {
        for (auto& note : notesPlaying) {
            midi.addEvent(juce::MidiMessage::noteOff(channel, note.number), samplePosition);
        }
        notesPlaying.clear();
        currentStep = -1;
        sampleCounter = 0.0;
        samplesPerStep = 0.0; // so the next entry fires step 0 on its first sample
    }

    template<typename Container>
    void setNotes(const Container& newNotes) {
        notes.assign(newNotes.begin(), newNotes.end());
        if (currentStep >= getNumSteps()) {
            currentStep = -1;
        }
    }

    int getNumSteps() const {
        return static_cast<int>(notes.size());
    }

    bool isEmpty() const {
        return notes.empty();
    }

    bool isLastStep() const {
        return getNumSteps() > 0 && currentStep == getNumSteps() - 1;
    }

    bool isPlaying() const {
        return currentStep > -1;
    }

private:
    std::vector<Note> notes;
    std::atomic<int> currentStep = -1;
    double sampleCounter = 0.0;
    double samplesPerStep = 0.0;
    std::vector<Note> notesPlaying;

    void updateSamplesPerStep(double bpm, double sampleRate) {
        const double beatsPerSecond = bpm / 60.0;
        const double stepLength = notes[currentStep].length;
        const double secondsPerStep = stepLength / beatsPerSecond;
        samplesPerStep = secondsPerStep * sampleRate;
    }
};
