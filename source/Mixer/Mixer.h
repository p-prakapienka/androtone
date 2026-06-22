#pragma once

#include "../Sequencer/StepSequencer.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>

#include "../Engine/Processors/Reverb.h"
#include "../Engine/Processors/Delay.h"

class Mixer {
public:
    static constexpr int numTracks = StepSequencer::numTracks;

    Mixer() {
        for (auto& s : sends1) {
            s.store(0.0f);
        }
        for (auto& s : sends2) {
            s.store(0.0f);
        }
        for (auto& v : trackVolumes) {
            v.store(1.0f);
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

        // The delay is wired the same way: a 100%-wet send return contributing
        // only the delayed signal, scaled per track by the delay send knobs.
        delay = std::make_unique<Delay>();
    }

    void prepare(double sampleRate) {
        if (delay != nullptr) {
            delay->prepare(sampleRate);
        }
        if (reverb != nullptr) {
            reverb->prepare(sampleRate);
        }
    }

    void mix(
        juce::AudioBuffer<float>& output,
        const std::array<juce::AudioBuffer<float>, numTracks>& tracks
    ) {
        std::array<float, numTracks> sends1Amounts = {};
        std::array<float, numTracks> sends2Amounts = {};
        std::array<float, numTracks> volumes = {};

        for (int t = 0; t < numTracks; t++) {
            sends1Amounts[t] = sends1[t].load();
            sends2Amounts[t] = sends2[t].load();
            volumes[t] = trackVolumes[t].load();
        }

        const float master = masterVolume.load();

        const int numChannels = output.getNumChannels();
        const int numSamples = output.getNumSamples();
        const int rightChannel = numChannels > 1 ? 1 : 0;

        for (int i = 0; i < numSamples; i++) {
            float dryLeft = 0.0f;
            float dryRight = 0.0f;
            float send1Left = 0.0f;
            float send1Right = 0.0f;
            float send2Left = 0.0f;
            float send2Right = 0.0f;

            // Sum the dry mix (scaled by volume), the delay send (send1), and the
            // reverb send (send2), each scaled by their per-track send knobs.
            for (int t = 0; t < numTracks; t++) {
                const float left = tracks[t].getSample(0, i);
                const float right = tracks[t].getSample(rightChannel, i);
                dryLeft += left * volumes[t];
                dryRight += right * volumes[t];
                send1Left += left * sends1Amounts[t];
                send1Right += right * sends1Amounts[t];
                send2Left += left * sends2Amounts[t];
                send2Right += right * sends2Amounts[t];
            }

            // Delay the send (send1): the delay replaces send1Left/send1Right
            // with the wet (delayed) signal, which we fold into the dry mix.
            if (delay != nullptr) {
                delay->process(send1Left, send1Right);
                dryLeft += send1Left;
                dryRight += send1Right;
            }

            // Reverberate the send (send2): the reverb replaces send2Left/
            // send2Right with the wet signal, which we fold into the dry mix.
            if (reverb != nullptr) {
                reverb->process(send2Left, send2Right);
                dryLeft += send2Left;
                dryRight += send2Right;
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
        if (delay != nullptr) {
            delay->reset();
        }
    }

    void setTrackVolume(int trackIndex, float vol) {
        trackVolumes[trackIndex].store(vol);
    }

    float getTrackVolume(int trackIndex) const {
        return trackVolumes[trackIndex].load();
    }

    void setTrackDelaySend(int trackIndex, float amount) {
        sends1[trackIndex].store(amount);
    }

    float getTrackDelaySend(int trackIndex) const {
        return sends1[trackIndex].load();
    }

    void setTrackReverbSend(int trackIndex, float amount) {
        sends2[trackIndex].store(amount);
    }

    float getTrackReverbSend(int trackIndex) const {
        return sends2[trackIndex].load();
    }

    void setMasterVolume(float vol) {
        masterVolume.store(vol);
    }

    float getMasterVolume() const {
        return masterVolume.load();
    }

private:
    std::array<std::atomic<float>, numTracks> sends1; // delay sends
    std::array<std::atomic<float>, numTracks> sends2; // reverb sends
    std::array<std::atomic<float>, numTracks> trackVolumes;
    std::atomic<float> masterVolume;

    std::unique_ptr<Processor> delay;
    std::unique_ptr<Processor> reverb;
};
