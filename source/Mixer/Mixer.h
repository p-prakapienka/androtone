#pragma once

#include "../Constants.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>

#include "../Engine/Processors/Reverb.h"
#include "../Engine/Processors/Delay.h"

// Track mixer: per-track volume, pan, activator (mute), solo and two sends feeding the delay and
// reverb returns, then the main volume. Every control is an atomic written by the UI thread and
// read once per block by the audio thread; gains ramp across the block to avoid zipper noise.
//
// Signal flow per track:   input ─┬─ pre sends ─────────────────────────────┐
//                                 └─ volume · pan · activator/solo ─┬─ main │
//                                                                   └─ post sends → returns → main
// Solo silences every non-soloed track (dry and sends); the returns stay audible, carrying the
// soloed tracks' sends. A muted track's post sends are silent; its pre sends keep feeding.
class Mixer {
public:
    static constexpr int NUM_RETURNS = 2;
    enum Return { DELAY_RETURN = 0, REVERB_RETURN = 1 };

    Mixer() {
        for (int t = 0; t < NUM_TRACKS; t++) {
            volume[idx(t)].store(1.0f);
            pan[idx(t)].store(0.0f);
            active[idx(t)].store(true);
            soloed[idx(t)].store(false);
            for (int r = 0; r < NUM_RETURNS; r++) {
                sends[idx(r)][idx(t)].store(0.0f);
            }
        }
        for (int r = 0; r < NUM_RETURNS; r++) {
            returnLevel[idx(r)].store(1.0f);
            returnPre[idx(r)].store(false);
        }
        masterVolume.store(0.75f);

        // Both returns are 100%-wet effects: the dry signal reaches the main through the track
        // mix, so a return contributes only the processed signal.
        auto verb = std::make_unique<Reverb>();
        verb->setDryLevel(0.0f);
        verb->setWetLevel(1.0f);
        returns[REVERB_RETURN] = std::move(verb);
        returns[DELAY_RETURN] = std::make_unique<Delay>();
    }

    void prepare(double sampleRate) {
        for (auto& fx : returns) {
            fx->prepare(sampleRate);
        }
    }

    void reset() {
        for (auto& fx : returns) {
            fx->reset();
        }
    }

    void mix(juce::AudioBuffer<float>& output, const std::array<juce::AudioBuffer<float>, NUM_TRACKS>& tracks) {
        const int numChannels = output.getNumChannels();
        const int numSamples = output.getNumSamples();
        const int rightChannel = numChannels > 1 ? 1 : 0;
        if (numSamples == 0) {
            return;
        }

        bool anySolo = false;
        for (int t = 0; t < NUM_TRACKS; t++) {
            anySolo = anySolo || soloed[idx(t)].load();
        }

        // Target gains for this block.
        std::array<Gains, NUM_TRACKS> target {};
        for (int t = 0; t < NUM_TRACKS; t++) {
            const bool audible = !anySolo || soloed[idx(t)].load();
            const bool on = active[idx(t)].load() && audible;
            const float v = volume[idx(t)].load();
            const float p = pan[idx(t)].load();
            // Balance law: centre is unity on both sides; turning one way lowers the other side.
            target[idx(t)].left = on ? v * juce::jmin(1.0f, 1.0f - p) : 0.0f;
            target[idx(t)].right = on ? v * juce::jmin(1.0f, 1.0f + p) : 0.0f;
            for (int r = 0; r < NUM_RETURNS; r++) {
                const float amount = sends[idx(r)][idx(t)].load();
                if (returnPre[idx(r)].load()) {
                    target[idx(t)].sendLeft[idx(r)] = target[idx(t)].sendRight[idx(r)] = audible ? amount : 0.0f;
                } else {
                    target[idx(t)].sendLeft[idx(r)] = amount * target[idx(t)].left;
                    target[idx(t)].sendRight[idx(r)] = amount * target[idx(t)].right;
                }
            }
        }
        std::array<float, NUM_RETURNS> returnGains {};
        for (int r = 0; r < NUM_RETURNS; r++) {
            returnGains[idx(r)] = returnLevel[idx(r)].load();
        }
        const float master = masterVolume.load();
        const float ramp = 1.0f / static_cast<float>(numSamples);

        std::array<float, NUM_TRACKS> trackPeak {};
        float mainPeak = 0.0f;

        for (int i = 0; i < numSamples; i++) {
            const float a = (i + 1) * ramp; // 0→1 across the block
            float left = 0.0f, right = 0.0f;
            std::array<float, NUM_RETURNS> sendL {}, sendR {};

            for (int t = 0; t < NUM_TRACKS; t++) {
                const auto& from = current[idx(t)];
                const auto& to = target[idx(t)];
                const float inL = tracks[idx(t)].getSample(0, i);
                const float inR = tracks[idx(t)].getSample(rightChannel, i);
                const float outL = inL * (from.left + (to.left - from.left) * a);
                const float outR = inR * (from.right + (to.right - from.right) * a);
                left += outL;
                right += outR;
                trackPeak[idx(t)] = juce::jmax(trackPeak[idx(t)], std::abs(outL), std::abs(outR));
                for (int r = 0; r < NUM_RETURNS; r++) {
                    sendL[idx(r)] += inL * (from.sendLeft[idx(r)] + (to.sendLeft[idx(r)] - from.sendLeft[idx(r)]) * a);
                    sendR[idx(r)] += inR * (from.sendRight[idx(r)] + (to.sendRight[idx(r)] - from.sendRight[idx(r)]) * a);
                }
            }

            for (int r = 0; r < NUM_RETURNS; r++) {
                returns[idx(r)]->process(sendL[idx(r)], sendR[idx(r)]);
                left += sendL[idx(r)] * returnGains[idx(r)];
                right += sendR[idx(r)] * returnGains[idx(r)];
            }

            left *= master;
            right *= master;
            mainPeak = juce::jmax(mainPeak, std::abs(left), std::abs(right));
            output.setSample(0, i, left);
            if (numChannels > 1) {
                output.setSample(1, i, right);
            }
        }

        current = target;
        for (int t = 0; t < NUM_TRACKS; t++) {
            raisePeak(peaks[idx(t)], trackPeak[idx(t)]);
        }
        raisePeak(masterPeak, mainPeak);
    }

    // ---- controls (UI thread) ----

    void setTrackVolume(int t, float v) { volume[idx(t)].store(v); }
    float getTrackVolume(int t) const { return volume[idx(t)].load(); }
    void setTrackPan(int t, float p) { pan[idx(t)].store(juce::jlimit(-1.0f, 1.0f, p)); }
    float getTrackPan(int t) const { return pan[idx(t)].load(); }
    void setTrackActive(int t, bool on) { active[idx(t)].store(on); }
    bool isTrackActive(int t) const { return active[idx(t)].load(); }
    void setTrackSolo(int t, bool on) { soloed[idx(t)].store(on); }
    bool isTrackSoloed(int t) const { return soloed[idx(t)].load(); }

    void setSend(int r, int t, float amount) { sends[idx(r)][idx(t)].store(amount); }
    float getSend(int r, int t) const { return sends[idx(r)][idx(t)].load(); }
    void setTrackDelaySend(int t, float amount) { setSend(DELAY_RETURN, t, amount); }
    float getTrackDelaySend(int t) const { return getSend(DELAY_RETURN, t); }
    void setTrackReverbSend(int t, float amount) { setSend(REVERB_RETURN, t, amount); }
    float getTrackReverbSend(int t) const { return getSend(REVERB_RETURN, t); }

    void setReturnLevel(int r, float level) { returnLevel[idx(r)].store(level); }
    float getReturnLevel(int r) const { return returnLevel[idx(r)].load(); }
    void setReturnPre(int r, bool pre) { returnPre[idx(r)].store(pre); }
    bool isReturnPre(int r) const { return returnPre[idx(r)].load(); }

    void setMasterVolume(float v) { masterVolume.store(v); }
    float getMasterVolume() const { return masterVolume.load(); }

    // Highest peak since the last call (resets it). For meters.
    float takeTrackPeak(int t) { return peaks[idx(t)].exchange(0.0f); }
    float takeMasterPeak() { return masterPeak.exchange(0.0f); }

private:
    struct Gains {
        float left = 0.0f, right = 0.0f;
        std::array<float, NUM_RETURNS> sendLeft {}, sendRight {};
    };

    static size_t idx(int i) { return static_cast<size_t>(i); }

    static void raisePeak(std::atomic<float>& peak, float value) {
        float previous = peak.load();
        while (value > previous && !peak.compare_exchange_weak(previous, value)) {
        }
    }

    std::array<std::atomic<float>, NUM_TRACKS> volume, pan;
    std::array<std::atomic<bool>, NUM_TRACKS> active, soloed;
    std::array<std::array<std::atomic<float>, NUM_TRACKS>, NUM_RETURNS> sends;
    std::array<std::atomic<float>, NUM_RETURNS> returnLevel;
    std::array<std::atomic<bool>, NUM_RETURNS> returnPre;
    std::atomic<float> masterVolume;
    std::array<std::atomic<float>, NUM_TRACKS> peaks {};
    std::atomic<float> masterPeak { 0.0f };

    std::array<Gains, NUM_TRACKS> current {}; // gains reached at the end of the last block
    std::array<std::unique_ptr<Processor>, NUM_RETURNS> returns;
};
