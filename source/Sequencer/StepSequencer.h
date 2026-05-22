#pragma once

#include "Pattern.h"
#include "Track.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

class StepSequencer {
public:
    static constexpr int numTracks = 2;

    StepSequencer() {
        bpm = 80.0;
        playing = false;
        numSteps = 0;
        currentSampleRate = 44100.0;
        lastPlayingState = false;

        tracks[0].setChannel(1);
        tracks[0].setPattern(Pattern::strangerThingsArp);
        tracks[1].setChannel(2);
        tracks[1].setPattern(Pattern::strangerThingsBass);

        for (auto& track : tracks) {
            auto trackNumSteps = track.getNumSteps();
            if (trackNumSteps > numSteps) {
                numSteps = trackNumSteps;
            }
        }
    }

    void prepareToPlay(double sampleRate) {
        currentSampleRate = sampleRate;
    }

    void processBlock(juce::MidiBuffer& midi, int numSamples) {
        const bool shouldPlay = playing.load();

        if (!shouldPlay && lastPlayingState) {
            for (auto& track : tracks) {
                track.stop(midi, 0);
            }
            lastPlayingState = false;
        }
        lastPlayingState = shouldPlay;

        if (!shouldPlay) {
            return;
        }

        const double currentBpm = bpm.load();
        for (auto& track : tracks) {
            track.processBlock(midi, numSamples, currentBpm, currentSampleRate);
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

    Track& getTrack(int trackIndex) {
        return tracks[trackIndex];
    }

private:
    std::array<Track, numTracks> tracks;
    std::atomic<double> bpm;
    std::atomic<bool> playing;
    std::atomic<int> numSteps;
    double currentSampleRate;
    bool lastPlayingState;
};
