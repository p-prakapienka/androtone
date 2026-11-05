#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

#include "../Devices.h"
#include "../SoundSources/AndroSynth.h"

// A track's insert chain: resonant filter (off / low-pass / high-pass) then a compressor
// (feed-forward, peak-detecting, linked stereo). Processes the track buffer in place.
class TrackEffects {
public:
    void prepare(double newSampleRate) {
        sampleRate = newSampleRate;
        reset();
        attackCoefficient = std::exp(-1.0 / (0.005 * sampleRate));
        releaseCoefficient = std::exp(-1.0 / (0.120 * sampleRate));
    }

    void reset() {
        for (auto& f : filters) {
            f.reset();
        }
        envelope = 0.0;
    }

    void process(juce::AudioBuffer<float>& buffer, int numSamples, const DeviceParams& params) {
        const int mode = static_cast<int>(params.get(EffectParam::FilterMode));
        const double cutoff = params.get(EffectParam::Cutoff);
        const double resonance = params.get(EffectParam::Resonance);
        const double threshold = params.get(EffectParam::Threshold);
        const double ratio = juce::jmax(1.0, static_cast<double>(params.get(EffectParam::Ratio)));
        const double makeup = juce::Decibels::decibelsToGain(static_cast<double>(params.get(EffectParam::Makeup)));
        const bool compress = threshold < -0.01 || makeup > 1.0001;
        const int channels = juce::jmin(2, buffer.getNumChannels());

        if (mode == 0 && !compress) {
            return;
        }

        for (int i = 0; i < numSamples; i++) {
            std::array<double, 2> frame {};
            for (int ch = 0; ch < channels; ch++) {
                double x = buffer.getSample(ch, i);
                if (mode != 0) {
                    const auto [low, high] = filters[static_cast<size_t>(ch)].process(x, cutoff, resonance, sampleRate);
                    x = mode == 1 ? low : high;
                }
                frame[static_cast<size_t>(ch)] = x;
            }

            if (compress) {
                const double peak = juce::jmax(std::abs(frame[0]), std::abs(frame[1]));
                const double coefficient = peak > envelope ? attackCoefficient : releaseCoefficient;
                envelope = coefficient * envelope + (1.0 - coefficient) * peak;
                const double levelDb = juce::Decibels::gainToDecibels(envelope, -120.0);
                const double overDb = levelDb - threshold;
                const double reductionDb = overDb > 0.0 ? overDb * (1.0 - 1.0 / ratio) : 0.0;
                gainReductionDb = reductionDb;
                const double gain = juce::Decibels::decibelsToGain(-reductionDb) * makeup;
                frame[0] *= gain;
                frame[1] *= gain;
            }

            for (int ch = 0; ch < channels; ch++) {
                buffer.setSample(ch, i, static_cast<float>(frame[static_cast<size_t>(ch)]));
            }
        }
    }

    double getGainReductionDb() const { return gainReductionDb; }

private:
    double sampleRate = 44100.0;
    std::array<StateVariableFilter, 2> filters;
    double envelope = 0.0;
    double attackCoefficient = 0.0, releaseCoefficient = 0.0;
    double gainReductionDb = 0.0;
};
