#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

// Natron's built-in drum kit, synthesised (not sampled) so it ships with no third-party audio.
// Deterministic: the same buffers every time, which keeps renders testable.
namespace DrumKit {
    constexpr double sampleRate = 48000.0;
    constexpr int numSounds = 8;

    // Kick, snare, closed hat, open hat, clap, low tom, high tom, cowbell.
    const std::array<const char*, numSounds>& names();

    // Mono buffers at DrumKit::sampleRate.
    std::array<juce::AudioBuffer<float>, numSounds> generate();
}
