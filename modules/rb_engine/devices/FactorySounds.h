#pragma once

namespace rb
{

// Factory samples are synthesised by our own code on first use, so the app ships no
// third-party audio. Each sound has a stable id used in "factory:<id>" sample references.
namespace FactorySounds
{
    struct Info
    {
        juce::String id;
        juce::String name;
        juce::String category; // "Kick", "Snare", "Hat", "Perc", "Tom", "Cymbal", "Tonal", "FX"
    };

    const std::vector<Info>& list();

    // Renders the sound at 48 kHz. Returns nullptr for an unknown id. Deterministic.
    SamplePtr render (const juce::String& id);
}

} // namespace rb
