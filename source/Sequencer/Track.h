#pragma once

#include "Clip.h"
#include "../Constants.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

class Track {
public:
    void processBlock(juce::MidiBuffer& midi, int numSamples, double samplesPerBeat) {
        const int processedSamples = clips[currentClipIndex].processBlock(
            midi, numSamples, channel, samplesPerBeat, /*stopOnBarLastStep=*/nextClipIndex >= 0
        );

        if (processedSamples >= numSamples) {
            return;
        }

        currentClipIndex.store(nextClipIndex);
        nextClipIndex = -1;

        clips[currentClipIndex].processBlock(
            midi, numSamples - processedSamples, channel, samplesPerBeat, false, processedSamples
        );
    }

    void stop(juce::MidiBuffer& midi, int samplePosition) {
        clips[currentClipIndex].stop(midi, samplePosition, channel);
        nextClipIndex = -1;
    }

    void setChannel(int newChannel) {
        channel = newChannel;
    }

    template<typename Container>
    void updateClip(const Container& notes, int clipIndex) {
        if (clipIndex < 0 || clipIndex >= MAX_CLIPS) {
            return;
        }

        clips[clipIndex].setNotes(notes);
    }

    const Clip& getClip(int index) const {
        return clips[index];
    }

    const Clip& getCurrentClip() const {
        return clips[currentClipIndex];
    }

    void setCurrentClip(int clipIndex) {
        if (clipIndex < 0 || clipIndex >= MAX_CLIPS) {
            return;
        }

        if (clips[currentClipIndex].isPlaying()) {
            nextClipIndex = clipIndex;
        } else {
            currentClipIndex = clipIndex;
            nextClipIndex = -1;
        }
    }

    int getCurrentClipIndex() const {
        return currentClipIndex;
    }

    int getNextClipIndex() const {
        return nextClipIndex;
    }

private:
    int channel = 1;
    std::array<Clip, MAX_CLIPS> clips;
    std::atomic<int> currentClipIndex = 0;
    std::atomic<int> nextClipIndex = -1;
};
