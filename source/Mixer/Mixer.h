#pragma once

#include "../Sequencer/StepSequencer.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>

#include "Engine/Processors/Processor.h"
#include "Engine/Processors/Reverb.h"

class Mixer {
public:
    static constexpr int numTracks = StepSequencer::numTracks;

    Mixer() {
        for (auto& v : trackVolumes) {
            v.store(1.0f);
        }
        for (auto& s : reverbSends) {
            s.store(0.0f);
        }
        masterVolume.store(0.75f);

        // The dry signal already reaches the master through the normal mix, so
        // the reverb acts as a 100%-wet send return: it contributes only the
        // reverberated signal, scaled per track by the send knobs. Configured
        // through the concrete Reverb, then held behind the Processor interface
        // so the effect can be swapped without the mixer knowing the type.
        auto verb = std::make_unique<Reverb>();
        verb->setDryLevel(0.0f);
        verb->setWetLevel(1.0f);
        reverb = std::move(verb);
    }

    void prepare(double sampleRate) {
        if (reverb != nullptr) {
            reverb->prepare(sampleRate);
        }
    }

    void mix(
        juce::AudioBuffer<float>& output,
        const std::array<juce::AudioBuffer<float>, numTracks>& tracks
    ) {
        std::array<float, numTracks> volumes;
        std::array<float, numTracks> sends;

        for (int t = 0; t < numTracks; t++) {
            volumes[t] = trackVolumes[t].load();
            sends[t] = reverbSends[t].load();
        }

        const float master = masterVolume.load();

        const int numChannels = output.getNumChannels();
        const int numSamples = output.getNumSamples();
        const int rightChannel = numChannels > 1 ? 1 : 0;

        for (int i = 0; i < numSamples; i++) {
            float dryLeft = 0.0f;
            float dryRight = 0.0f;
            float sendLeft = 0.0f;
            float sendRight = 0.0f;

            // Sum the dry mix (scaled by volume) and the reverb send (scaled by
            // each track's send knob) for this sample frame.
            for (int t = 0; t < numTracks; t++) {
                const float left = tracks[t].getSample(0, i);
                const float right = tracks[t].getSample(rightChannel, i);
                dryLeft += left * volumes[t];
                dryRight += right * volumes[t];
                sendLeft += left * sends[t];
                sendRight += right * sends[t];
            }

            // Reverberate the send: the reverb replaces sendLeft/sendRight with
            // the wet signal, which we fold into the dry mix.
            if (reverb != nullptr) {
                reverb->process(sendLeft, sendRight);
                dryLeft += sendLeft;
                dryRight += sendRight;
            }

            output.setSample(0, i, dryLeft * master);
            if (numChannels > 1) {
                output.setSample(1, i, dryRight * master);
            }
        }
    }

    void reset() {
        if (reverb != nullptr) {
            reverb->reset();
        }
    }

    void setTrackVolume(int trackIndex, float vol) {
        trackVolumes[trackIndex].store(vol);
    }

    float getTrackVolume(int trackIndex) const {
        return trackVolumes[trackIndex].load();
    }

    void setTrackReverbSend(int trackIndex, float amount) {
        reverbSends[trackIndex].store(amount);
    }

    float getTrackReverbSend(int trackIndex) const {
        return reverbSends[trackIndex].load();
    }

    void setMasterVolume(float vol) {
        masterVolume.store(vol);
    }

    float getMasterVolume() const {
        return masterVolume.load();
    }

private:
    std::array<std::atomic<float>, numTracks> trackVolumes;
    std::array<std::atomic<float>, numTracks> reverbSends;
    std::atomic<float> masterVolume;

    std::unique_ptr<Processor> reverb;
};
