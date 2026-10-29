#pragma once

namespace rb
{

struct Note
{
    double start = 0.0;       // beats from clip start
    double length = 0.25;     // beats
    int pitch = 60;           // MIDI note (drums: drumBaseNote + pad)
    float velocity = 0.8f;    // 0..1
    float pitchOffset = 0.0f; // semitones; drum clips only (EDIT-10 Pitches tab)

    // Timing before the last quantize, so "quantize off" can restore it (EDIT-10).
    double unquantizedStart = -1.0;

    bool operator== (const Note& o) const
    {
        return juce::exactlyEqual (start, o.start) && juce::exactlyEqual (length, o.length) && pitch == o.pitch
            && juce::exactlyEqual (velocity, o.velocity) && juce::exactlyEqual (pitchOffset, o.pitchOffset);
    }
};

// Identifies which parameter an automation lane drives.
struct ParamTarget
{
    enum Device { instrument = 0, effect1 = 1, effect2 = 2, pad = 3 };

    int device = instrument;
    int padIndex = 0; // device == pad only
    int paramIndex = 0;

    bool operator== (const ParamTarget& o) const
    {
        return device == o.device && paramIndex == o.paramIndex && (device != pad || padIndex == o.padIndex);
    }
};

struct AutomationPoint
{
    double beat = 0.0; // clip position
    float value = 0.0f;
};

struct AutomationLane
{
    ParamTarget target;
    std::vector<AutomationPoint> points; // sorted by beat

    float valueAt (double beat) const;
};

// Immutable once shared: edits build a new Clip (see Edits.h). Notes are kept sorted by start.
struct Clip
{
    int lengthBars = 1;
    double loopStart = 0.0;  // beats
    double loopLength = 4.0; // beats
    std::vector<Note> notes;
    std::vector<AutomationLane> automation;

    double lengthBeats() const { return lengthBars * (double) beatsPerBar; }
    double loopEnd() const { return loopStart + loopLength; }

    void sortNotes();
    const AutomationLane* findLane (const ParamTarget&) const;
};

using ClipPtr = std::shared_ptr<const Clip>;

struct DeviceState
{
    int type = 0;              // InstrumentType or EffectType, by context
    std::vector<float> values; // one per ParamSpec of the device, in spec order
    juce::String presetName;
};

struct DrumPad
{
    juce::String name;
    juce::String sampleRef;
    std::vector<float> values; // Pad Sampler params (see DeviceRegistry::getPadSpec)
};

struct PerformanceOptions
{
    enum Layout { pads = 0, alternate = 1 }; // alternate = Single (drums) or Keys (melodic)

    int layout = pads;
    int octave = 0;    // melodic only, -3..+3
    int repeat = 0;    // index into getRepeatRates(); 0 = off
    int selectedPad = 0;
};

struct Track
{
    int id = 0;
    juce::String name;
    int colour = 0;

    juce::String presetName;
    juce::String presetCategory;
    bool presetModified = false;

    InstrumentType instrumentType = InstrumentType::analog;
    DeviceState instrument;
    juce::StringArray macros; // param ids shown on the first tab (Analog/Wave)
    std::array<DeviceState, 2> effects;
    std::array<DrumPad, numDrumPads> pads; // drumKit only
    juce::String samplerSampleRef;         // sampler only

    float volume = 0.8f; // slider position 0..1, see volumeToGain
    bool mute = false;
    bool solo = false;

    PerformanceOptions perf;

    std::array<ClipPtr, numScenes> slots;

    bool isDrum() const { return instrumentType == InstrumentType::drumKit; }
    bool isMelodic() const { return ! isDrum(); }
    bool hasClips() const;
};

struct Set
{
    juce::String name { "New Set" };
    double tempo = 120.0;
    bool metronome = false;
    bool inKey = false;
    int root = 0;  // pitch class, 0 = C
    int scale = 0; // index into getScales()
    int nextTrackId = 1;
    std::vector<Track> tracks;

    int indexOfTrack (int trackId) const;
    const Track* findTrack (int trackId) const;
    Track* findTrack (int trackId);
    bool hasAnyClip() const;
    bool hasMelodicTrack() const;

    // Length of the shortest clip in the set in bars, or 0 if there are none (CAP-4).
    int shortestClipBars() const;
    // Length of the longest clip in a scene in beats (EXP-1), 0 if the scene is empty.
    double sceneLengthBeats (int scene) const;
};

// Mixer slider position (0..1) to linear gain: 0.8 is unity, 1.0 is +6 dB, 0 is silence.
float volumeToGain (float sliderPosition);
float volumeToDecibels (float sliderPosition);

// Repeat rates for PAD-7, index 0 = off.
struct RepeatRate
{
    const char* label;
    double beats;
};
const std::vector<RepeatRate>& getRepeatRates();

} // namespace rb
