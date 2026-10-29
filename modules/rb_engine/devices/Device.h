#pragma once

namespace rb
{

// A note event inside one audio block, produced by the sequencer or by live input.
struct NoteEvent
{
    int sampleOffset = 0;     // position inside the block
    int pitch = 60;
    float velocity = 0.0f;    // 0 for note-off
    float pitchOffset = 0.0f; // semitones, drum clips only
    bool isNoteOn() const { return velocity > 0.0f; }
};

// Instruments render a stereo block from note events.
//
// Threading: created and prepared on the message thread, then used only by the audio thread
// until they are destroyed (again on the message thread). setParameters/setPadParameters/
// setSample are called by the audio thread at the start of every block with the values from
// the current snapshot (cheap: implementations should only recompute what changed).
class InstrumentProcessor
{
public:
    virtual ~InstrumentProcessor() = default;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;

    virtual void setParameters (const float* values, int numValues) = 0;

    // Drum Kit only.
    virtual void setPadParameters (int /*pad*/, const float* /*values*/, int /*numValues*/) {}

    // slot = pad index (Drum Kit) or 0 (Sampler). The pointer stays valid until the next call
    // with a different pointer; when it changes, stop any voice still reading the old one.
    virtual void setSample (int /*slot*/, const SampleData* /*sample*/) {}

    // Renders numSamples into dry (and, for the Drum Kit, the pad-FX send into send). Both
    // buffers are stereo and already cleared. Events are sorted by sampleOffset.
    virtual void process (juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>& send,
                          const NoteEvent* events, int numEvents, int numSamples) = 0;

    // Immediately silences all voices (used on stop and when samples change).
    virtual void allNotesOff() = 0;

    // Expression from MIDI controllers and the transport, called by the audio thread at the
    // start of a block. Pitch bend in semitones; pressure 0..1 for one note, or for every
    // note when note is -1 (channel pressure).
    virtual void setPitchBend (float /*semitones*/) {}
    virtual void setPressure (int /*note*/, float /*pressure*/) {}
    virtual void setTempo (double /*bpm*/) {}
};

class EffectProcessor
{
public:
    virtual ~EffectProcessor() = default;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;
    virtual void setParameters (const float* values, int numValues) = 0;

    // In-place processing of a stereo buffer.
    virtual void process (juce::AudioBuffer<float>& buffer, int numSamples) = 0;

    // Transport tempo for tempo-synced effects (Delay, Auto Filter LFO).
    virtual void setTempo (double /*bpm*/) {}
};

} // namespace rb
