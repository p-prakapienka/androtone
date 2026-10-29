#pragma once

namespace rb
{

// Everything the audio thread needs about one track, frozen at publish time.
struct TrackSnapshot
{
    int id = 0;
    InstrumentType instrumentType = InstrumentType::analog;

    // Processors are created and prepared on the message thread and owned by the snapshots
    // that reference them; only the audio thread calls their processing methods.
    std::shared_ptr<InstrumentProcessor> instrument;
    std::array<std::shared_ptr<EffectProcessor>, 2> effects;

    std::vector<float> instrumentValues;
    std::array<std::vector<float>, 2> effectValues;
    std::array<std::vector<float>, numDrumPads> padValues;
    std::array<SamplePtr, numDrumPads> padSamples;
    SamplePtr samplerSample;

    float gain = 1.0f;         // mixer volume with mute/solo applied
    double repeatBeats = 0.0;  // 0 = repeat off

    std::array<ClipPtr, numScenes> clips;
};

struct EngineSnapshot
{
    juce::uint64 sequence = 0;
    double tempo = 120.0;
    bool metronome = false;
    std::vector<TrackSnapshot> tracks;

    int indexOf (int trackId) const
    {
        for (size_t i = 0; i < tracks.size(); ++i)
            if (tracks[i].id == trackId)
                return (int) i;
        return -1;
    }
};

using SnapshotPtr = std::shared_ptr<const EngineSnapshot>;

} // namespace rb
