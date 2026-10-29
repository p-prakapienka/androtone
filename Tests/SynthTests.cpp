#include "TestHelpers.h"

using namespace rb;

namespace
{

// Plays an instrument directly (no session) at 48 kHz and records its stereo output.
struct SynthRig
{
    static constexpr double sampleRate = 48000.0;
    static constexpr int blockSize = 256;

    explicit SynthRig (InstrumentType t)
        : type (t), spec (getInstrumentSpec (t)), values (spec.defaults()), instrument (createInstrument (t))
    {
        instrument->prepare (sampleRate, blockSize);
    }

    void set (const juce::String& id, float value)
    {
        const int index = spec.indexOf (id);
        jassert (index >= 0);
        if (index >= 0)
            values[(size_t) index] = spec.params[(size_t) index].clamp (value);
    }

    void apply (const ParamValues& preset)
    {
        values = spec.defaults();
        for (auto& [id, v] : preset)
            set (id, v);
    }

    // Queues a note event at an absolute sample position.
    void note (juce::int64 at, int pitch, float velocity) { pending.push_back ({ at, pitch, velocity }); }
    void noteAtSeconds (double seconds, int pitch, float velocity) { note (toSamples (seconds), pitch, velocity); }

    static juce::int64 toSamples (double seconds) { return (juce::int64) std::llround (seconds * sampleRate); }

    // Renders n samples in blocks; onBlock (if set) runs before each block (parameter changes).
    void render (juce::int64 n, int block = blockSize)
    {
        juce::AudioBuffer<float> dry (2, block), send (2, block);
        std::vector<NoteEvent> events;

        while (n > 0)
        {
            const int count = (int) juce::jmin ((juce::int64) block, n);
            if (onBlock)
                onBlock (position);

            events.clear();
            std::stable_sort (pending.begin(), pending.end(), [] (auto& a, auto& b) { return a.at < b.at; });
            for (auto it = pending.begin(); it != pending.end();)
            {
                if (it->at < position + count)
                {
                    NoteEvent e;
                    e.sampleOffset = (int) juce::jmax ((juce::int64) 0, it->at - position);
                    e.pitch = it->pitch;
                    e.velocity = it->velocity;
                    events.push_back (e);
                    it = pending.erase (it);
                }
                else
                {
                    ++it;
                }
            }

            dry.clear();
            send.clear();
            instrument->setParameters (values.data(), (int) values.size());
            juce::AudioBuffer<float> d (dry.getArrayOfWritePointers(), 2, 0, count);
            juce::AudioBuffer<float> s (send.getArrayOfWritePointers(), 2, 0, count);
            instrument->process (d, s, events.data(), (int) events.size(), count);

            for (int i = 0; i < count; ++i)
            {
                left.push_back (dry.getSample (0, i));
                right.push_back (dry.getSample (1, i));
                sendPeak = juce::jmax (sendPeak, std::abs (send.getSample (0, i)), std::abs (send.getSample (1, i)));
            }

            position += count;
            n -= count;
        }
    }

    void renderSeconds (double seconds, int block = blockSize) { render (toSamples (seconds), block); }

    float peak (juce::int64 from, juce::int64 to) const
    {
        float p = 0.0f;
        for (auto i = (size_t) juce::jmax ((juce::int64) 0, from); i < (size_t) juce::jmin (to, (juce::int64) left.size()); ++i)
            p = juce::jmax (p, std::abs (left[i]), std::abs (right[i]));
        return p;
    }

    float peak() const { return peak (0, (juce::int64) left.size()); }

    float rms (juce::int64 from, juce::int64 to) const
    {
        double sum = 0.0;
        juce::int64 count = 0;
        for (auto i = (size_t) juce::jmax ((juce::int64) 0, from); i < (size_t) juce::jmin (to, (juce::int64) left.size()); ++i)
        {
            sum += 0.5 * ((double) left[i] * left[i] + (double) right[i] * right[i]);
            ++count;
        }
        return count > 0 ? (float) std::sqrt (sum / (double) count) : 0.0f;
    }

    bool allFinite() const
    {
        for (size_t i = 0; i < left.size(); ++i)
            if (! std::isfinite (left[i]) || ! std::isfinite (right[i]))
                return false;
        return true;
    }

    float maxStep (juce::int64 from, juce::int64 to) const
    {
        float m = 0.0f;
        for (auto i = (size_t) juce::jmax ((juce::int64) 1, from); i < (size_t) juce::jmin (to, (juce::int64) left.size()); ++i)
            m = juce::jmax (m, std::abs (left[i] - left[i - 1]), std::abs (right[i] - right[i - 1]));
        return m;
    }

    // Frequency from rising zero crossings of the left channel.
    double frequency (juce::int64 from, juce::int64 to) const
    {
        int crossings = 0;
        juce::int64 first = -1, last = -1;
        for (auto i = (size_t) juce::jmax ((juce::int64) 1, from); i < (size_t) juce::jmin (to, (juce::int64) left.size()); ++i)
            if (left[i - 1] <= 0.0f && left[i] > 0.0f)
            {
                if (first < 0)
                    first = (juce::int64) i;
                last = (juce::int64) i;
                ++crossings;
            }
        return crossings > 1 ? (crossings - 1) * sampleRate / (double) (last - first) : 0.0;
    }

    struct Pending
    {
        juce::int64 at;
        int pitch;
        float velocity;
    };

    InstrumentType type;
    const DeviceSpec& spec;
    std::vector<float> values;
    std::unique_ptr<InstrumentProcessor> instrument;
    std::vector<Pending> pending;
    std::vector<float> left, right;
    std::function<void (juce::int64)> onBlock;
    juce::int64 position = 0;
    float sendPeak = 0.0f;
};

// A pure sine at the played pitch, no filtering or modulation.
void makeSine (SynthRig& rig)
{
    rig.set ("filterType", 0);
    rig.set ("cutoff", 20000.0f);
    rig.set ("filterEnv", 0.0f);
    rig.set ("keyTrack", 0.0f);
    rig.set ("resonance", 0.0f);
    rig.set ("velToFilter", 0.0f);
    if (rig.type == InstrumentType::analog)
    {
        rig.set ("osc1Shape", 0.0f);
        rig.set ("osc2Shape", 0.0f);
        rig.set ("oscMix", 0.0f);
        rig.set ("drift", 0.0f);
    }
    else
    {
        rig.set ("osc1Table", 0.0f);
        rig.set ("osc1Pos", 0.0f);
        rig.set ("osc2Level", 0.0f);
        rig.set ("unison", 0.0f);
    }
}

} // namespace

class SynthTests : public juce::UnitTest
{
public:
    SynthTests() : juce::UnitTest ("Synths", "devices") {}

    void runTest() override
    {
        for (auto type : { InstrumentType::analog, InstrumentType::wave })
        {
            const auto prefix = getInstrumentDisplayName (type) + ": ";
            const auto& spec = getInstrumentSpec (type);

            beginTest (prefix + "spec is well formed (DEV-11, DEV-15)");
            {
                expect ((int) spec.params.size() <= maxDeviceParams);
                expectEquals (spec.groups.size(), 6);
                juce::StringArray ids;
                for (auto& p : spec.params)
                {
                    expect (spec.groups.contains (p.group), p.id + " has an unknown group");
                    expect (! ids.contains (p.id), "duplicate id " + p.id);
                    ids.add (p.id);
                    expect (p.defaultValue >= p.minValue && p.defaultValue <= p.maxValue, p.id);
                }
                expect (spec.indexOf ("volume") >= 0);
                expect (spec.indexOf ("cutoff") >= 0);
            }

            beginTest (prefix + "silent before a note, sounds, silent after the release tail");
            {
                SynthRig rig (type);
                rig.noteAtSeconds (0.1, 60, 1.0f);
                rig.noteAtSeconds (0.6, 60, 0.0f);
                rig.renderSeconds (3.0);

                expect (rig.allFinite());
                expectEquals (rig.peak (0, SynthRig::toSamples (0.1)), 0.0f);
                const float playing = rig.peak (SynthRig::toSamples (0.1), SynthRig::toSamples (0.6));
                expectGreaterThan (playing, 0.05f);
                expectLessThan (playing, 1.0f);
                expectLessThan (rig.peak (SynthRig::toSamples (2.5), SynthRig::toSamples (3.0)), 1.0e-5f);
                expectEquals (rig.sendPeak, 0.0f);
            }

            beginTest (prefix + "note start has no click");
            {
                SynthRig rig (type);
                rig.noteAtSeconds (0.05, 48, 1.0f);
                rig.renderSeconds (0.1);
                // The first samples after the note-on stay small (attack ramp).
                const auto on = SynthRig::toSamples (0.05);
                expectLessThan (rig.peak (on, on + 8), 0.1f);
            }

            beginTest (prefix + "16 simultaneous notes steal voices with bounded level");
            {
                SynthRig rig (type);
                for (int i = 0; i < 16; ++i)
                    rig.note (100 + i, 40 + i * 3, 1.0f);
                rig.renderSeconds (1.0);
                for (int i = 0; i < 16; ++i)
                    rig.noteAtSeconds (1.0, 40 + i * 3, 0.0f);
                rig.renderSeconds (3.0);

                expect (rig.allFinite());
                expectGreaterThan (rig.peak(), 0.05f);
                expectLessThan (rig.peak(), 4.0f);
                expectLessThan (rig.peak (SynthRig::toSamples (3.5), SynthRig::toSamples (4.0)), 1.0e-5f);

                // Rapid repeated chords keep working (no stuck voices).
                for (int r = 0; r < 20; ++r)
                    for (int i = 0; i < 12; ++i)
                    {
                        rig.noteAtSeconds (4.0 + r * 0.05, 50 + i, 0.9f);
                        rig.noteAtSeconds (4.03 + r * 0.05, 50 + i, 0.0f);
                    }
                rig.renderSeconds (5.0);
                expect (rig.allFinite());
                expectLessThan (rig.peak (SynthRig::toSamples (8.5), SynthRig::toSamples (9.0)), 1.0e-5f);
            }

            beginTest (prefix + "parameter changes during playback don't click");
            {
                SynthRig rig (type);
                makeSine (rig);
                rig.set ("ampSustain", 1.0f);
                rig.noteAtSeconds (0.0, 57, 1.0f);
                rig.renderSeconds (0.3);
                const float staticStep = rig.maxStep (SynthRig::toSamples (0.1), SynthRig::toSamples (0.3));

                int counter = 0;
                rig.onBlock = [&] (juce::int64)
                {
                    const bool flip = (++counter / 4) % 2 == 0;
                    rig.set ("volume", flip ? 0.0f : -30.0f);
                    rig.set ("cutoff", flip ? 20000.0f : 300.0f);
                    rig.set ("resonance", flip ? 0.0f : 0.6f);
                    rig.set (type == InstrumentType::analog ? "oscMix" : "osc1Level", flip ? 0.0f : 0.6f);
                    rig.set (type == InstrumentType::analog ? "spread" : "osc1Pos", flip ? 0.0f : 0.2f);
                };
                rig.renderSeconds (1.0);
                expect (rig.allFinite());
                const float changingStep = rig.maxStep (SynthRig::toSamples (0.3), SynthRig::toSamples (1.3));
                logMessage ("max sample step: static " + juce::String (staticStep, 4) + ", while changing "
                            + juce::String (changingStep, 4));
                expectLessThan (changingStep, juce::jmax (0.03f, staticStep * 2.0f));
            }

            beginTest (prefix + "allNotesOff silences within one block");
            {
                SynthRig rig (type);
                rig.set ("ampRelease", 5.0f);
                for (int p : { 48, 55, 60, 64 })
                    rig.note (0, p, 1.0f);
                rig.renderSeconds (0.5);
                expectGreaterThan (rig.peak (SynthRig::toSamples (0.4), SynthRig::toSamples (0.5)), 0.05f);
                rig.instrument->allNotesOff();
                rig.render (SynthRig::blockSize);
                const auto start = (juce::int64) rig.left.size();
                rig.render (SynthRig::blockSize * 4);
                expectEquals (rig.peak (start, (juce::int64) rig.left.size()), 0.0f);
            }

            beginTest (prefix + "reset clears every voice");
            {
                SynthRig rig (type);
                rig.note (0, 60, 1.0f);
                rig.renderSeconds (0.2);
                rig.instrument->reset();
                const auto start = (juce::int64) rig.left.size();
                rig.renderSeconds (0.1);
                expectEquals (rig.peak (start, (juce::int64) rig.left.size()), 0.0f);
            }

            beginTest (prefix + "A4 plays at 440 Hz");
            {
                SynthRig rig (type);
                makeSine (rig);
                rig.note (0, 69, 1.0f);
                rig.renderSeconds (1.2);
                expectWithinAbsoluteError (rig.frequency (SynthRig::toSamples (0.1), SynthRig::toSamples (1.1)), 440.0, 1.0);

                SynthRig octave (type);
                makeSine (octave);
                octave.set ("osc1Octave", 3); // +1 octave
                octave.set ("transpose", -12.0f);
                octave.note (0, 57, 1.0f);
                octave.renderSeconds (1.2);
                expectWithinAbsoluteError (octave.frequency (SynthRig::toSamples (0.1), SynthRig::toSamples (1.1)), 220.0, 1.0);
            }

            beginTest (prefix + "mono mode returns to the held note; legato glides");
            {
                SynthRig rig (type);
                makeSine (rig);
                rig.set ("voiceMode", 1);
                rig.note (0, 57, 1.0f);
                rig.noteAtSeconds (0.5, 69, 1.0f);
                rig.noteAtSeconds (1.0, 69, 0.0f);
                rig.renderSeconds (1.5);
                expectWithinAbsoluteError (rig.frequency (SynthRig::toSamples (0.1), SynthRig::toSamples (0.5)), 220.0, 1.5);
                expectWithinAbsoluteError (rig.frequency (SynthRig::toSamples (0.6), SynthRig::toSamples (1.0)), 440.0, 1.5);
                expectWithinAbsoluteError (rig.frequency (SynthRig::toSamples (1.1), SynthRig::toSamples (1.5)), 220.0, 1.5);

                SynthRig glide (type);
                makeSine (glide);
                glide.set ("voiceMode", 2);
                glide.set ("glide", 0.3f);
                glide.note (0, 57, 1.0f);
                glide.noteAtSeconds (0.5, 69, 1.0f);
                glide.renderSeconds (1.5);
                const double during = glide.frequency (SynthRig::toSamples (0.52), SynthRig::toSamples (0.6));
                expectGreaterThan (during, 225.0);
                expectLessThan (during, 435.0);
                expectWithinAbsoluteError (glide.frequency (SynthRig::toSamples (1.0), SynthRig::toSamples (1.5)), 440.0, 1.5);
            }

            beginTest (prefix + "renders 8 held voices fast enough for phones");
            {
                SynthRig rig (type);
                for (int i = 0; i < 8; ++i)
                    rig.note (0, 48 + i * 3, 0.9f);
                rig.render (SynthRig::toSamples (0.1));
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                rig.renderSeconds (10.0);
                const auto elapsed = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
                const double factor = 10.0 / juce::jmax (1.0e-6, elapsed);
                logMessage (getInstrumentDisplayName (type) + ": 8 voices render at " + juce::String (factor, 1)
                            + "x real time");
                expect (rig.allFinite());
               #if JUCE_DEBUG
                expectGreaterThan (factor, 1.0);
               #else
                expectGreaterThan (factor, 20.0);
               #endif
            }

            beginTest (prefix + "factory presets are valid and render sound");
            {
                int count = 0;
                for (auto& preset : getFactoryPresets())
                {
                    if (preset.type != type)
                        continue;
                    ++count;

                    expect (getPresetCategories().contains (preset.category), preset.name + ": category " + preset.category);
                    for (auto& [id, v] : preset.values)
                    {
                        expect (spec.indexOf (id) >= 0, preset.name + ": unknown parameter " + id);
                        if (spec.indexOf (id) >= 0)
                        {
                            auto& p = spec.params[(size_t) spec.indexOf (id)];
                            expect (v >= p.minValue && v <= p.maxValue, preset.name + ": " + id + " out of range");
                        }
                    }
                    expect (preset.macros.size() > 0 && preset.macros.size() <= 8, preset.name + ": macro count");
                    for (auto& m : preset.macros)
                        expect (spec.indexOf (m) >= 0, preset.name + ": unknown macro " + m);
                    for (auto& fx : preset.effects)
                        expect (fx.type != EffectType::none && fx.name.isNotEmpty(), preset.name + ": effect slot");

                    SynthRig rig (type);
                    rig.apply (preset.values);
                    const bool bass = preset.category == "Bass";
                    const std::vector<int> notes = bass ? std::vector<int> { 36, 43 } : std::vector<int> { 60, 64, 67 };
                    for (size_t i = 0; i < notes.size(); ++i)
                    {
                        rig.noteAtSeconds (0.05 + 0.4 * (double) i, notes[i], 0.85f);
                        rig.noteAtSeconds (0.35 + 0.4 * (double) i, notes[i], 0.0f);
                    }
                    rig.noteAtSeconds (1.4, notes[0], 1.0f);
                    rig.noteAtSeconds (2.4, notes[0], 0.0f);
                    rig.renderSeconds (3.0);

                    expect (rig.allFinite(), preset.name + ": NaN");
                    const float peak = rig.peak();
                    logMessage (preset.name + " (" + preset.category + "): peak " + juce::String (peak, 3)
                                + ", rms " + juce::String (rig.rms (0, SynthRig::toSamples (3.0)), 3));
                    expectGreaterThan (peak, 0.03f, preset.name + " is (nearly) silent");
                    expectLessThan (peak, 1.5f, preset.name + " is too loud");
                }
                expectGreaterThan (count, 10);
            }
        }

        beginTest ("Factory synth presets have unique names and cover the categories");
        {
            juce::StringArray names;
            std::map<juce::String, int> perCategory;
            for (auto& p : getFactoryPresets())
            {
                if (p.type != InstrumentType::analog && p.type != InstrumentType::wave)
                    continue;
                expect (! names.contains (p.name), "duplicate preset name " + p.name);
                names.add (p.name);
                ++perCategory[p.category];
            }
            for (auto cat : { "Bass", "Keys", "Lead", "Pad", "Pluck", "Mallets", "FX" })
                expectGreaterThan (perCategory[cat], 2, cat);
        }
    }
};

static SynthTests synthTests;
