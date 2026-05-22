#pragma once

#include "../Sequencer/StepSequencer.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>

class Mixer {
public:
    static constexpr int numTracks = StepSequencer::numTracks;

    Mixer() {
        for (auto& v : trackVolumes) {
            v.store(1.0f);
        }
        masterVolume.store(0.75f);
    }

    void mix(
        juce::AudioBuffer<float>& output,
        const std::array<juce::AudioBuffer<float>, numTracks>& tracks
    ) {
        std::array<float, numTracks> volumes;

        for (int t = 0; t < numTracks; t++) {
            volumes[t] = trackVolumes[t].load();
        }

        const float master = masterVolume.load();

        const int numChannels = output.getNumChannels();
        const int numSamples = output.getNumSamples();

        for (int c = 0; c < numChannels; c++) {
            for (int i = 0; i < numSamples; i++) {
                float sample = 0.0f;

                for (int t = 0; t < numTracks; t++) {
                    sample += tracks[t].getSample(c, i) * volumes[t];
                }

                output.setSample(c, i, sample * master);
            }
        }
    }

    void setTrackVolume(int trackIndex, float vol) {
        trackVolumes[trackIndex].store(vol);
    }

    float getTrackVolume(int trackIndex) const {
        return trackVolumes[trackIndex].load();
    }

    void setMasterVolume(float vol) {
        masterVolume.store(vol);
    }

    float getMasterVolume() const {
        return masterVolume.load();
    }

private:
    std::array<std::atomic<float>, numTracks> trackVolumes;
    std::atomic<float> masterVolume;
};
