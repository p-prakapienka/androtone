#pragma once

#include "Clip.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

class Track {
public:
    static constexpr int maxClips = 32;

    void processBlock(juce::MidiBuffer& midi, int numSamples, double bpm, double sampleRate) {
        if (currentClipIndex < 0 || currentClipIndex >= maxClips || clips[currentClipIndex].isEmpty()) {
            return;
        }

        const int processedSamples = clips[currentClipIndex].processBlock(
            midi, numSamples, channel, bpm, sampleRate, /*stopOnBarLastStep=*/nextClipIndex >= 0
        );

        if (processedSamples >= numSamples) {
            return;
        }

        currentClipIndex = nextClipIndex;
        nextClipIndex = -1;

        if (currentClipIndex < 0 || currentClipIndex >= maxClips || clips[currentClipIndex].isEmpty()) {
            return;
        }

        clips[currentClipIndex].processBlock(
            midi, numSamples - processedSamples, channel, bpm, sampleRate, false, processedSamples
        );
    }

    void stop(juce::MidiBuffer& midi, int samplePosition) {
        if (currentClipIndex >= 0 && currentClipIndex < maxClips) {
            clips[currentClipIndex].stop(midi, samplePosition, channel);
        }
        nextClipIndex = -1;
    }

    void setNextClip(int index) {
        if (index == currentClipIndex) {
            nextClipIndex = -1;
        } else {
            nextClipIndex = index;
        }
    }

    void setChannel(int newChannel) {
        channel = newChannel;
    }

    template<typename Container>
    void setClip(const Container& notes, int clipIndex) {
        clips[clipIndex].setNotes(notes);
    }

    const Clip& getClip(int index) const {
        return clips[index];
    }

    const Clip& getCurrentClip() const {
        return clips[currentClipIndex];
    }

    int getCurrentClipIndex() const {
        return currentClipIndex;
    }

    int getNextClipIndex() const {
        return nextClipIndex;
    }

private:
    int channel = 1;
    std::array<Clip, maxClips> clips;
    int currentClipIndex = 0;
    int nextClipIndex = -1;
};
