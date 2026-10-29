#pragma once

namespace rb
{

// Renders a set without an audio device: audio export (EXP-1) and tests.
class OfflineRenderer
{
public:
    // Returns false from the callback to cancel. Progress is 0..1.
    using Progress = std::function<bool (float progress)>;

    // Every scene that has clips, top to bottom; each scene lasts as long as its longest clip.
    // Followed by tailSeconds of silence for effect tails.
    static juce::AudioBuffer<float> renderScenes (const Set&, const juce::File& setFolder, SampleLibrary&,
                                                  double sampleRate, double tailSeconds = 2.0,
                                                  Progress progress = nullptr);

    // Renders `beats` beats of one scene (tests, previews).
    static juce::AudioBuffer<float> renderScene (const Set&, const juce::File& setFolder, SampleLibrary&,
                                                 int scene, double beats, double sampleRate);

    enum class Format { wav16, wav24, flac };
    static bool write (const juce::AudioBuffer<float>&, double sampleRate, Format, const juce::File&);
};

} // namespace rb
