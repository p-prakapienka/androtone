#pragma once

#include <rb_engine/rb_engine.h>

namespace rbtest
{

// Records every note event with its absolute sample position. Silent.
struct ProbeLog
{
    struct Entry
    {
        juce::int64 sample;
        int pitch;
        float velocity;
        float pitchOffset;
    };

    std::vector<Entry> entries;
    std::vector<int> sampleSlots; // slots that received a sample
    std::vector<std::vector<float>> parameterHistory; // values passed to setParameters, per block

    std::vector<Entry> noteOns() const
    {
        std::vector<Entry> ons;
        for (auto& e : entries)
            if (e.velocity > 0.0f)
                ons.push_back (e);
        return ons;
    }
};

class ProbeInstrument : public rb::InstrumentProcessor
{
public:
    explicit ProbeInstrument (std::shared_ptr<ProbeLog> l) : log (std::move (l)) {}

    void prepare (double, int) override { position = 0; }
    void reset() override {}
    void setParameters (const float* values, int numValues) override
    {
        log->parameterHistory.emplace_back (values, values + numValues);
    }
    void process (juce::AudioBuffer<float>&, juce::AudioBuffer<float>&, const rb::NoteEvent* events, int numEvents,
                  int numSamples) override
    {
        for (int i = 0; i < numEvents; ++i)
            log->entries.push_back ({ position + events[i].sampleOffset, events[i].pitch, events[i].velocity,
                                      events[i].pitchOffset });
        position += numSamples;
    }
    void allNotesOff() override {}

private:
    std::shared_ptr<ProbeLog> log;
    juce::int64 position = 0;
};

// Installs probe instruments for the lifetime of the object; one log per created instrument.
struct ProbeScope
{
    ProbeScope()
    {
        rb::setInstrumentFactoryOverride ([this] (rb::InstrumentType)
        {
            logs.push_back (std::make_shared<ProbeLog>());
            return std::make_unique<ProbeInstrument> (logs.back());
        });
    }

    ~ProbeScope() { rb::setInstrumentFactoryOverride (nullptr); }

    std::vector<std::shared_ptr<ProbeLog>> logs;
};

// A session + document rendering offline at 48 kHz, 120 BPM unless changed.
struct Rig
{
    static constexpr double sampleRate = 48000.0;
    static constexpr int blockSize = 256;

    rb::SampleLibrary samples;
    rb::SessionProcessor session;
    rb::Document document { session, samples };
    juce::AudioBuffer<float> buffer { 2, blockSize };
    juce::int64 rendered = 0;

    Rig() { session.setOffline (true); session.prepare (sampleRate, blockSize); }
    ~Rig() { session.releaseResources(); }

    void open (const rb::Set& set) { document.openInMemory (set); }

    void post (rb::Command::Type type, int trackId = 0, int slot = 0, int pitch = 0, float velocity = 0.0f)
    {
        rb::Command c;
        c.type = type;
        c.trackId = trackId;
        c.slot = slot;
        c.pitch = pitch;
        c.velocity = velocity;
        session.post (c);
    }

    // Renders exactly n samples (in blocks; the last block may be shorter).
    void render (juce::int64 n)
    {
        while (n > 0)
        {
            const int count = (int) juce::jmin ((juce::int64) blockSize, n);
            session.process (buffer, count);
            rendered += count;
            n -= count;
        }
    }

    void renderBeats (double beats) { render (beatsToSamples (beats)); }

    juce::int64 beatsToSamples (double beats) const
    {
        return (juce::int64) std::llround (beats * 60.0 / document.get().tempo * sampleRate);
    }
};

inline rb::TrackPreset simplePreset (rb::InstrumentType type = rb::InstrumentType::analog)
{
    rb::TrackPreset p;
    p.name = "Test";
    p.category = type == rb::InstrumentType::drumKit ? "Drums" : "Keys";
    p.type = type;
    return p;
}

inline rb::ClipPtr clipWithNotes (std::initializer_list<rb::Note> notes, int bars = 1)
{
    auto clip = std::make_shared<rb::Clip>();
    clip->lengthBars = bars;
    clip->loopLength = bars * 4.0;
    clip->notes = notes;
    clip->sortNotes();
    return clip;
}

inline rb::Note note (double start, int pitch, double length = 0.25, float velocity = 0.8f)
{
    rb::Note n;
    n.start = start;
    n.pitch = pitch;
    n.length = length;
    n.velocity = velocity;
    return n;
}

} // namespace rbtest
