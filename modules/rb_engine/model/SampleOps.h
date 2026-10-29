#pragma once

namespace rb
{

//==============================================================================
// Min/max overview of a sample for drawing waveforms at any zoom (SMP-2). Built once per
// sample; columns are answered from coarse buckets when zoomed out and from the frames
// themselves when zoomed in. Channels are merged (min of mins, max of maxes).
class WaveformPeaks
{
public:
    struct Range
    {
        float min = 0.0f;
        float max = 0.0f;
    };

    WaveformPeaks() = default;
    explicit WaveformPeaks (SamplePtr sample, int framesPerBucket = 64);

    bool isEmpty() const { return numFrames == 0; }
    int getNumFrames() const { return numFrames; }
    const SampleData* getSample() const { return sample.get(); }

    // Peaks of numColumns equal columns spanning the frames [startFrame, endFrame).
    // Columns outside the sample are empty ranges.
    std::vector<Range> getColumns (double startFrame, double endFrame, int numColumns) const;

private:
    Range scanFrames (int start, int end) const;

    SamplePtr sample;
    int numFrames = 0;
    int bucketSize = 64;
    std::vector<Range> buckets;
};

//==============================================================================
// Turns a raw recording into a sample (SMP-3, SMP-4): trims the start to the first transient
// and trailing silence, normalises quiet takes up to about -6 dBFS (at most +24 dB) and adds
// short fades at the cut points. Returns nullptr when nothing audible was recorded.
struct RecordingCleanupResult
{
    SamplePtr sample;
    int trimmedStart = 0; // frames removed at the start
    int trimmedEnd = 0;   // frames removed at the end
    float gainDb = 0.0f;  // normalisation applied
};

RecordingCleanupResult cleanUpRecording (const juce::AudioBuffer<float>& raw, double sampleRate,
                                         const juce::String& name);

//==============================================================================
// Browser helpers (DEV-3, SMP-2).

// True when every word of query occurs in text (case-insensitive). An empty query matches.
bool matchesSearch (const juce::String& text, const juce::String& query);

// Indices of presets for a browser view: a non-empty search spans all categories, otherwise
// the category must match (an empty category matches all). type < 0 accepts any instrument.
std::vector<int> filterPresets (const std::vector<TrackPreset>&, const juce::String& category,
                                const juce::String& search, int type = -1);

// Display name for a sample reference: the factory sound's name or the file name without
// extension.
juce::String sampleDisplayName (const juce::String& ref);

// Samples stored inside a set's own folder (<setFolder>/samples), as "set:<file>" references.
juce::StringArray listSetSamples (const juce::File& setFolder);

// A recording made in this set (only these can be saved to the user library, SMP-6).
bool isSetRecording (const juce::String& ref);

// "rec-YYYYMMDD-HHMMSS.wav", with "-2", "-3"... appended if a file of that name exists in folder.
juce::File makeRecordingFile (const juce::File& folder, juce::Time time);

// First frame played for a sampleStart fraction (0..1).
int startFrameFor (float startFraction, int numFrames);

} // namespace rb
