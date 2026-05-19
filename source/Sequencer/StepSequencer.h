#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

struct Note {
    int number;
    double length;
    int velocity;
};

class StepSequencer {
public:
    static constexpr int numSteps = 32;
    static constexpr std::array<Note, numSteps> strangerThingsArp = {{
        //  Bar1
        { 60, 0.25, 100 }, //C4
        { 64, 0.25, 100 }, //E4
        { 67, 0.25, 100 }, //G4
        { 71, 0.25, 100 }, //B4
        { 72, 0.25, 100 }, //C5
        { 71, 0.25, 100 }, //B4
        { 67, 0.25, 100 }, //G4
        { 64, 0.25, 100 }, //E4
        { 60, 0.25, 100 }, //C4
        { 64, 0.25, 100 }, //E4
        { 67, 0.25, 100 }, //G4
        { 71, 0.25, 100 }, //B4
        { 72, 0.25, 100 }, //C5
        { 71, 0.25, 100 }, //B4
        { 67, 0.25, 100 }, //G4
        { 64, 0.25, 100 }, //E4
        //  Bar2
        { 52, 0.25, 100 }, //E3
        { 64, 0.25, 100 }, //E4
        { 67, 0.25, 100 }, //G4
        { 71, 0.25, 100 }, //B4
        { 72, 0.25, 100 }, //C5
        { 71, 0.25, 100 }, //B4
        { 67, 0.25, 100 }, //G4
        { 64, 0.25, 100 }, //E4
        { 52, 0.25, 100 }, //E3
        { 64, 0.25, 100 }, //E4
        { 67, 0.25, 100 }, //G4
        { 71, 0.25, 100 }, //B4
        { 72, 0.25, 100 }, //C5
        { 71, 0.25, 100 }, //B4
        { 67, 0.25, 100 }, //G4
        { 64, 0.25, 100 }, //E4
    }};

    StepSequencer() {
        notes = strangerThingsArp;
        bpm = 80.0;
        playing = false;
        currentSampleRate = 44100.0;
        currentStep = 0;
        sampleCounter = 0;
        samplesPerStep = 0.0;
        lastPlayingState = false;
    }

    void prepareToPlay(double sampleRate) {
        currentSampleRate = sampleRate;
        updateSamplesPerStep();
    }

    void processBlock(juce::MidiBuffer& midi, int numSamples) {
        bool shouldPlay = playing.load();

        if (!shouldPlay && lastPlayingState) {
            midi.addEvent(juce::MidiMessage::noteOff(1, notes[currentStep].number), 0);
            lastPlayingState = false;
        }
        lastPlayingState = shouldPlay;

        if (!shouldPlay) {
            return;
        }

        updateSamplesPerStep();

        for (int sample = 0; sample < numSamples; sample++) {
            if (sampleCounter >= samplesPerStep) {
                sampleCounter = 0;

                const int previousStep = currentStep;
                midi.addEvent(juce::MidiMessage::noteOff(1, notes[previousStep].number), sample);

                currentStep = (currentStep + 1) % numSteps;
                const Note& nextNote = notes[currentStep];
                midi.addEvent(
                    juce::MidiMessage::noteOn(1, nextNote.number, static_cast<juce::uint8>(nextNote.velocity)),
                    sample
                );

                updateSamplesPerStep();
            }
            ++sampleCounter;
        }
    }

    void setTempo(double newBpm) {
        bpm.store(newBpm);
    }

    void setPlaying(bool shouldPlay) {
        playing.store(shouldPlay);
    }

    bool isPlaying() const {
        return playing.load();
    }

    double getTempo() const {
        return bpm.load();
    }

    void setMelody(const std::array<Note, numSteps>& newMelody) {
        notes = newMelody;
    }

private:
    std::array<Note, numSteps> notes;
    std::atomic<double> bpm;
    std::atomic<bool> playing;
    double currentSampleRate;
    int currentStep;
    double sampleCounter;
    double samplesPerStep;
    bool lastPlayingState;

    void updateSamplesPerStep() {
        const double beatsPerSecond = bpm.load() / 60.0;
        const double stepLength = notes[currentStep].length;
        const double secondsPerStep = stepLength / beatsPerSecond;
        samplesPerStep = secondsPerStep * currentSampleRate;
    }
};
