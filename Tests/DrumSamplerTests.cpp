#include "TestHelpers.h"

using namespace rb;
using namespace rbtest;

namespace
{

constexpr double testRate = 48000.0;

// YIN pitch estimate (cumulative mean normalised difference) over n samples of x.
float estimatePitch (const float* x, int n, double sampleRate, float minHz = 60.0f, float maxHz = 1500.0f)
{
    const int maxLag = (int) (sampleRate / minHz);
    const int minLag = juce::jmax (2, (int) (sampleRate / maxHz));
    const int window = n - maxLag - 2;
    if (window <= 0)
        return 0.0f;

    std::vector<double> cmnd ((size_t) maxLag + 2, 1.0);
    double running = 0.0;
    for (int lag = 1; lag <= maxLag + 1; ++lag)
    {
        double d = 0.0;
        for (int i = 0; i < window; ++i)
        {
            const double e = (double) x[i] - (double) x[i + lag];
            d += e * e;
        }
        running += d;
        cmnd[(size_t) lag] = running > 0.0 ? d * lag / running : 1.0;
    }

    int best = -1;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        if (cmnd[(size_t) lag] < 0.15)
        {
            while (lag + 1 <= maxLag && cmnd[(size_t) lag + 1] < cmnd[(size_t) lag])
                ++lag;
            best = lag;
            break;
        }
    }
    if (best < 0)
    {
        best = minLag;
        for (int lag = minLag; lag <= maxLag; ++lag)
            if (cmnd[(size_t) lag] < cmnd[(size_t) best])
                best = lag;
    }

    const double a = cmnd[(size_t) best - 1], b = cmnd[(size_t) best], c = cmnd[(size_t) best + 1];
    const double denom = a - 2.0 * b + c;
    const double offset = std::abs (denom) > 1.0e-12 ? 0.5 * (a - c) / denom : 0.0;
    return (float) (sampleRate / (best + offset));
}

std::shared_ptr<SampleData> constantSample (int frames, float value = 0.5f, int channels = 1)
{
    auto s = std::make_shared<SampleData>();
    s->sampleRate = testRate;
    s->buffer.setSize (channels, frames);
    for (int ch = 0; ch < channels; ++ch)
        juce::FloatVectorOperations::fill (s->buffer.getWritePointer (ch), value, frames);
    return s;
}

std::shared_ptr<SampleData> sineSample (float hz, float seconds, double rate = testRate)
{
    auto s = std::make_shared<SampleData>();
    s->sampleRate = rate;
    const int frames = (int) (seconds * rate);
    s->buffer.setSize (1, frames);
    for (int i = 0; i < frames; ++i)
        s->buffer.setSample (0, i, 0.5f * std::sin (juce::MathConstants<float>::twoPi * hz * (float) ((double) i / rate)));
    return s;
}

NoteEvent noteOn (int offset, int pitch, float velocity = 1.0f, float pitchOffset = 0.0f)
{
    NoteEvent e;
    e.sampleOffset = offset;
    e.pitch = pitch;
    e.velocity = velocity;
    e.pitchOffset = pitchOffset;
    return e;
}

NoteEvent noteOff (int offset, int pitch)
{
    return noteOn (offset, pitch, 0.0f);
}

// Renders an instrument block by block into one long stereo buffer (dry and send).
struct InstrumentRig
{
    static constexpr int blockSize = 256;

    explicit InstrumentRig (InstrumentType type) : instrument (createInstrument (type))
    {
        instrument->prepare (testRate, blockSize);
        instrument->setParameters (getInstrumentSpec (type).defaults().data(), (int) getInstrumentSpec (type).params.size());
    }

    // Pad parameters by id, starting from the defaults.
    static std::vector<float> padValues (std::initializer_list<std::pair<const char*, float>> overrides = {})
    {
        auto values = getPadSpec().defaults();
        for (auto& [id, v] : overrides)
        {
            const int index = getPadSpec().indexOf (id);
            jassert (index >= 0);
            values[(size_t) index] = v;
        }
        return values;
    }

    void setPad (int pad, const std::vector<float>& values) { instrument->setPadParameters (pad, values.data(), (int) values.size()); }

    // Renders `total` samples; events carry absolute positions from the start of this call.
    void render (int total, std::vector<NoteEvent> events = {})
    {
        const int start = dry.getNumSamples();
        dry.setSize (2, start + total, true, true);
        send.setSize (2, start + total, true, true);

        juce::AudioBuffer<float> blockDry (2, blockSize), blockSend (2, blockSize);
        for (int pos = 0; pos < total; pos += blockSize)
        {
            const int n = juce::jmin (blockSize, total - pos);
            std::vector<NoteEvent> blockEvents;
            for (auto e : events)
                if (e.sampleOffset >= pos && e.sampleOffset < pos + n)
                {
                    e.sampleOffset -= pos;
                    blockEvents.push_back (e);
                }

            if (beforeBlock)
                beforeBlock();

            blockDry.clear();
            blockSend.clear();
            juce::AudioBuffer<float> d (blockDry.getArrayOfWritePointers(), 2, 0, n);
            juce::AudioBuffer<float> s (blockSend.getArrayOfWritePointers(), 2, 0, n);
            instrument->process (d, s, blockEvents.data(), (int) blockEvents.size(), n);
            for (int ch = 0; ch < 2; ++ch)
            {
                dry.copyFrom (ch, start + pos, d, ch, 0, n);
                send.copyFrom (ch, start + pos, s, ch, 0, n);
            }
        }
    }

    float peak (int channel, int from, int to, bool fromSend = false) const
    {
        const auto& b = fromSend ? send : dry;
        to = juce::jmin (to, b.getNumSamples());
        return from < to ? b.getMagnitude (channel, from, to - from) : 0.0f;
    }

    // Index of the last sample louder than threshold, or -1.
    int lastAudible (float threshold = 1.0e-3f) const
    {
        for (int i = dry.getNumSamples(); --i >= 0;)
            if (std::abs (dry.getSample (0, i)) > threshold || std::abs (dry.getSample (1, i)) > threshold)
                return i;
        return -1;
    }

    std::unique_ptr<InstrumentProcessor> instrument;
    juce::AudioBuffer<float> dry { 2, 0 }, send { 2, 0 };
    std::function<void()> beforeBlock;
};

bool isSilent (float x) { return std::abs (x) < 1.0e-12f; }

} // namespace

class DrumSamplerTests : public juce::UnitTest
{
public:
    DrumSamplerTests() : juce::UnitTest ("Drum Kit and Sampler", "devices") {}

    void runTest() override
    {
        testSpecs();
        testFactorySounds();
        testDrumKit();
        testSampler();
        testPresets();
        testThroughEngine();
    }

private:
    void testSpecs()
    {
        beginTest ("Pad, Drum Kit and Sampler specs have the fixed ids, order and groups");
        {
            const juce::StringArray padIds { "transpose", "filterType", "cutoff", "resonance", "detune", "attack", "hold",
                                             "envMode", "decay", "pan", "fxType", "fx1", "fx2", "velTarget", "velAmount",
                                             "velVolume", "send", "choke", "gain", "sampleStart" };
            const auto& pad = getPadSpec();
            expectEquals (pad.typeId, juce::String ("pad"));
            expectEquals ((int) pad.params.size(), padIds.size());
            for (int i = 0; i < padIds.size() && i < (int) pad.params.size(); ++i)
                expectEquals (pad.params[(size_t) i].id, padIds[i]);
            expect (pad.groups == juce::StringArray { "Page 1", "Page 2", "Pad FX", "Sample" });
            expectEquals (pad.params[(size_t) pad.indexOf ("send")].group, juce::String ("Pad FX"));
            expectEquals (pad.params[(size_t) pad.indexOf ("fxType")].choices.size(), 10);
            expectEquals (pad.params[(size_t) pad.indexOf ("hold")].defaultValue, 10.0f);

            const auto& kit = getInstrumentSpec (InstrumentType::drumKit);
            expectEquals (kit.typeId, juce::String ("drumKit"));
            expect (kit.indexOf ("volume") >= 0);

            const juce::StringArray samplerIds { "filterType", "cutoff", "resonance", "transpose", "detune", "attack", "decay",
                                                 "envMode", "release", "filterLfo", "lfoRate", "volume", "gain", "sampleStart" };
            const auto& sampler = getInstrumentSpec (InstrumentType::sampler);
            expectEquals (sampler.typeId, juce::String ("sampler"));
            expectEquals ((int) sampler.params.size(), samplerIds.size());
            for (int i = 0; i < samplerIds.size() && i < (int) sampler.params.size(); ++i)
                expectEquals (sampler.params[(size_t) i].id, samplerIds[i]);
            expect (sampler.groups == juce::StringArray { "Main", "Global", "Sample" });
            expectEquals (sampler.params[(size_t) sampler.indexOf ("envMode")].defaultValue, 1.0f);
        }
    }

    void testFactorySounds()
    {
        beginTest ("Every factory sound renders: audible, finite, <= 0 dBFS, <= 3 s, deterministic");
        {
            const juce::StringArray categories { "Kick", "Snare", "Clap", "Hat", "Tom", "Perc", "Cymbal", "FX", "Tonal" };
            const std::map<juce::String, int> minimum { { "Kick", 8 }, { "Snare", 7 }, { "Clap", 4 }, { "Hat", 8 }, { "Tom", 4 },
                                                        { "Perc", 10 }, { "Cymbal", 4 }, { "FX", 6 }, { "Tonal", 12 } };
            std::map<juce::String, int> counts;
            juce::StringArray ids;

            expect (FactorySounds::list().size() >= 70);
            for (auto& info : FactorySounds::list())
            {
                expect (categories.contains (info.category), info.id + ": unknown category " + info.category);
                expect (! ids.contains (info.id), "duplicate id " + info.id);
                expect (info.id.startsWith (info.category.toLowerCase() + "-"), info.id + ": id needs its category prefix");
                expect (info.name.isNotEmpty());
                ids.add (info.id);
                counts[info.category]++;

                auto a = FactorySounds::render (info.id);
                expect (a != nullptr, info.id + " did not render");
                if (a == nullptr)
                    continue;

                expectEquals (a->sampleRate, 48000.0);
                expect (a->getNumFrames() > 0 && a->getNumFrames() <= 3 * 48000, info.id + " too long");
                expect (a->buffer.getNumChannels() == 1 || a->buffer.getNumChannels() == 2);

                float peak = 0.0f;
                bool finite = true;
                for (int ch = 0; ch < a->buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < a->getNumFrames(); ++i)
                    {
                        const float v = a->buffer.getSample (ch, i);
                        finite = finite && std::isfinite (v);
                        peak = juce::jmax (peak, std::abs (v));
                    }
                expect (finite, info.id + " has non-finite samples");
                expect (peak <= 1.0f, info.id + " clips");
                expect (peak > 0.5f, info.id + " is (nearly) silent");

                auto b = FactorySounds::render (info.id);
                bool same = b != nullptr && b->getNumFrames() == a->getNumFrames()
                         && b->buffer.getNumChannels() == a->buffer.getNumChannels();
                for (int ch = 0; same && ch < a->buffer.getNumChannels(); ++ch)
                    same = std::memcmp (a->buffer.getReadPointer (ch), b->buffer.getReadPointer (ch),
                                        sizeof (float) * (size_t) a->getNumFrames()) == 0;
                expect (same, info.id + " is not deterministic");
            }

            for (auto& [cat, min] : minimum)
                expect (counts[cat] >= min, cat + ": only " + juce::String (counts[cat]) + " sounds");

            expect (FactorySounds::render ("no-such-sound") == nullptr);
        }

        beginTest ("Tonal sounds are pitched at C3 (261.6 Hz)");
        {
            for (auto& info : FactorySounds::list())
            {
                if (info.category != "Tonal")
                    continue;
                auto s = FactorySounds::render (info.id);
                if (s == nullptr)
                    continue;
                const int start = 12000; // 250 ms in: past the attack transients
                const int length = 8192;
                expect (s->getNumFrames() >= start + length, info.id + " is too short to analyse");
                if (s->getNumFrames() < start + length)
                    continue;
                const float hz = estimatePitch (s->buffer.getReadPointer (0) + start, length, s->sampleRate);
                const float cents = 1200.0f * std::log2 (hz / 261.6256f);
                expect (std::abs (cents) < 35.0f, info.id + " is at " + juce::String (hz, 2) + " Hz");
            }
        }
    }

    void testDrumKit()
    {
        const int kick = drumBaseNote, snare = drumBaseNote + 1;

        beginTest ("A pad hit starts exactly at its event offset");
        {
            auto sample = constantSample (4800);
            InstrumentRig rig (InstrumentType::drumKit);
            rig.setPad (0, InstrumentRig::padValues());
            rig.instrument->setSample (0, sample.get());
            rig.render (512, { noteOn (300, kick) });

            bool silentBefore = true;
            for (int i = 0; i < 300; ++i)
                silentBefore = silentBefore && isSilent (rig.dry.getSample (0, i)) && isSilent (rig.dry.getSample (1, i));
            expect (silentBefore);
            expect (std::abs (rig.dry.getSample (0, 300)) > 0.0f);
            expect (rig.peak (0, 300, 512) > 0.2f);
        }

        beginTest ("Notes outside the pads and pads without a sample are silent");
        {
            auto sample = constantSample (4800);
            InstrumentRig rig (InstrumentType::drumKit);
            rig.instrument->setSample (0, sample.get());
            rig.render (1024, { noteOn (0, kick - 1), noteOn (0, kick + 16), noteOn (10, snare) });
            expectEquals (rig.peak (0, 0, 1024), 0.0f);
        }

        beginTest ("Choke: a choking pad stops the other choking pad");
        {
            auto sample = constantSample (48000);
            for (bool choke : { true, false })
            {
                InstrumentRig rig (InstrumentType::drumKit);
                const float on = choke ? 1.0f : 0.0f;
                rig.setPad (0, InstrumentRig::padValues ({ { "choke", on }, { "pan", -1.0f } })); // left only
                rig.setPad (1, InstrumentRig::padValues ({ { "choke", on }, { "pan", 1.0f } }));  // right only
                rig.instrument->setSample (0, sample.get());
                rig.instrument->setSample (1, sample.get());
                rig.render (9600, { noteOn (0, kick), noteOn (4800, snare) });

                expect (rig.peak (0, 4000, 4800) > 0.2f);  // pad 0 rings...
                expect (rig.peak (1, 4000, 4800) < 1.0e-3f);
                expect (rig.peak (1, 6000, 9600) > 0.2f);  // ...pad 1 starts...
                if (choke)
                    expect (rig.peak (0, 4800 + 960, 9600) < 1.0e-4f, "choked pad still sounds"); // ...and chokes pad 0
                else
                    expect (rig.peak (0, 4800 + 960, 9600) > 0.2f, "pad without choke was cut");
            }
        }

        beginTest ("Retriggering a pad keeps the previous hit ringing");
        {
            auto sample = constantSample (48000, 0.25f);
            InstrumentRig rig (InstrumentType::drumKit);
            rig.setPad (0, InstrumentRig::padValues ({ { "velVolume", 0.0f } }));
            rig.instrument->setSample (0, sample.get());
            rig.render (4800, { noteOn (0, kick), noteOn (2400, kick) });
            expectWithinAbsoluteError (rig.dry.getSample (0, 2000), 0.25f, 0.01f);
            expectWithinAbsoluteError (rig.dry.getSample (0, 4000), 0.5f, 0.01f); // two hits summed
        }

        beginTest ("Gate mode stops at note-off, Trig mode plays on");
        {
            auto sample = constantSample (48000);
            for (int mode : { 0, 1 })
            {
                InstrumentRig rig (InstrumentType::drumKit);
                rig.setPad (0, InstrumentRig::padValues ({ { "envMode", (float) mode } }));
                rig.instrument->setSample (0, sample.get());
                rig.render (9600, { noteOn (0, kick), noteOff (2400, kick) });
                expect (rig.peak (0, 2000, 2400) > 0.2f);
                if (mode == 1)
                    expect (rig.peak (0, 2400 + 960, 9600) < 1.0e-4f, "Gate hit kept sounding after note-off");
                else
                    expect (rig.peak (0, 2400 + 960, 9600) > 0.2f, "Trig hit stopped at note-off");
            }
        }

        beginTest ("Decay shortens the hit; hold and decay at maximum play the whole sample");
        {
            auto sample = constantSample (24000);
            InstrumentRig full (InstrumentType::drumKit);
            full.setPad (0, InstrumentRig::padValues());
            full.instrument->setSample (0, sample.get());
            full.render (30000, { noteOn (0, kick) });
            expect (std::abs (full.lastAudible() - 24000) < 200);

            InstrumentRig shortHit (InstrumentType::drumKit);
            shortHit.setPad (0, InstrumentRig::padValues ({ { "hold", 0.05f }, { "decay", 0.1f } }));
            shortHit.instrument->setSample (0, sample.get());
            shortHit.render (30000, { noteOn (0, kick) });
            expect (shortHit.lastAudible() < (int) (0.16 * testRate));
            expect (shortHit.lastAudible() > (int) (0.05 * testRate));
        }

        beginTest ("Send buffer gets signal only when the pad's send is above zero");
        {
            auto sample = constantSample (4800);
            for (float amount : { 0.0f, 0.5f })
            {
                InstrumentRig rig (InstrumentType::drumKit);
                rig.setPad (0, InstrumentRig::padValues ({ { "send", amount } }));
                rig.instrument->setSample (0, sample.get());
                rig.render (2048, { noteOn (0, kick) });
                const float dryPeak = rig.peak (0, 0, 2048);
                const float sendPeak = rig.peak (0, 0, 2048, true);
                expect (dryPeak > 0.2f);
                if (amount > 0.0f)
                    expectWithinAbsoluteError (sendPeak, dryPeak * amount, 0.01f);
                else
                    expectEquals (sendPeak, 0.0f);
            }
        }

        beginTest ("Pitch offset +12 doubles the playback speed");
        {
            auto sample = constantSample (9600);
            InstrumentRig normal (InstrumentType::drumKit), up (InstrumentType::drumKit);
            for (auto* rig : { &normal, &up })
            {
                rig->setPad (0, InstrumentRig::padValues());
                rig->instrument->setSample (0, sample.get());
            }
            normal.render (12000, { noteOn (0, kick) });
            up.render (12000, { noteOn (0, kick, 1.0f, 12.0f) });
            expect (std::abs (normal.lastAudible() - 9600) < 100);
            expect (std::abs (up.lastAudible() - 4800) < 100);

            // Transpose does the same, and the engine rate is honoured for other sample rates.
            auto slow = constantSample (4800);
            slow->sampleRate = 24000.0;
            InstrumentRig resampled (InstrumentType::drumKit);
            resampled.setPad (0, InstrumentRig::padValues ({ { "transpose", 12.0f } }));
            resampled.instrument->setSample (0, slow.get());
            resampled.render (12000, { noteOn (0, kick) });
            expect (std::abs (resampled.lastAudible() - 4800) < 100);
        }

        beginTest ("Sample start offsets the playhead and gain scales the hit");
        {
            auto sample = constantSample (9600, 0.25f);
            InstrumentRig rig (InstrumentType::drumKit);
            rig.setPad (0, InstrumentRig::padValues ({ { "sampleStart", 0.5f }, { "gain", 6.0f }, { "velVolume", 0.0f } }));
            rig.instrument->setSample (0, sample.get());
            rig.render (12000, { noteOn (0, kick) });
            expect (std::abs (rig.lastAudible() - 4800) < 100);
            expectWithinAbsoluteError (rig.dry.getSample (0, 1000), 0.25f * dsp::dbToGain (6.0f), 0.01f);
        }

        beginTest ("Every playback FX type renders finite, audible output");
        {
            auto sample = FactorySounds::render ("snare-acoustic");
            for (int fx = 0; fx < 10; ++fx)
                for (int filter = 0; filter < 4; ++filter)
                {
                    InstrumentRig rig (InstrumentType::drumKit);
                    rig.setPad (0, InstrumentRig::padValues ({ { "fxType", (float) fx }, { "filterType", (float) filter },
                                                               { "cutoff", filter == 2 ? 200.0f : 5000.0f },
                                                               { "resonance", 0.5f }, { "fx1", 0.7f }, { "fx2", 0.3f },
                                                               { "velTarget", (float) (fx % 6) }, { "velAmount", 0.5f } }));
                    rig.instrument->setSample (0, sample.get());
                    rig.render (48000, { noteOn (0, kick, 0.8f), noteOff (2000, kick) });
                    bool finite = true;
                    for (int i = 0; i < rig.dry.getNumSamples(); ++i)
                        finite = finite && std::isfinite (rig.dry.getSample (0, i)) && std::isfinite (rig.dry.getSample (1, i));
                    expect (finite, "FX " + juce::String (fx) + " produced non-finite samples");
                    expect (rig.peak (0, 0, 4800) > 0.01f, "FX " + juce::String (fx) + " is silent");
                    expect (rig.peak (0, 0, 48000) < 8.0f, "FX " + juce::String (fx) + " blew up");
                }
        }

        beginTest ("Voices beyond 32 steal the oldest without blowing up");
        {
            auto sample = constantSample (48000, 0.02f);
            InstrumentRig rig (InstrumentType::drumKit);
            rig.setPad (0, InstrumentRig::padValues ({ { "velVolume", 0.0f } }));
            rig.instrument->setSample (0, sample.get());
            std::vector<NoteEvent> events;
            for (int i = 0; i < 48; ++i)
                events.push_back (noteOn (i * 100, kick));
            rig.render (9600, events);
            // At most 32 hits sound together once the fades are over.
            expect (rig.peak (0, 8000, 9600) <= 32 * 0.02f + 0.01f);
            expect (rig.peak (0, 8000, 9600) > 31 * 0.02f - 0.01f);
        }

        beginTest ("Changing a pad's sample stops hits reading the old one");
        {
            auto first = constantSample (48000, 0.5f);
            auto second = constantSample (48000, 0.25f);
            InstrumentRig rig (InstrumentType::drumKit);
            rig.instrument->setSample (0, first.get());
            rig.render (1024, { noteOn (0, kick) });
            expect (rig.peak (0, 512, 1024) > 0.2f);

            // Like the engine: the new pointer arrives at the start of a block, after which the
            // old sample may be freed.
            rig.instrument->setSample (0, second.get());
            first.reset();
            rig.render (4096);
            expect (rig.peak (0, 1024 + 1024, 1024 + 4096) < 1.0e-4f);

            rig.render (2048, { noteOn (0, kick) });
            expect (rig.peak (0, 1024 + 4096 + 512, 1024 + 4096 + 2048) > 0.1f);

            // Removing the sample silences the pad.
            rig.instrument->setSample (0, nullptr);
            second.reset();
            rig.render (2048, { noteOn (100, kick) });
            expect (rig.peak (0, 1024 + 4096 + 2048 + 1024, rig.dry.getNumSamples()) < 1.0e-4f);
        }

        beginTest ("allNotesOff silences the kit");
        {
            auto sample = constantSample (48000);
            InstrumentRig rig (InstrumentType::drumKit);
            rig.instrument->setSample (0, sample.get());
            rig.render (1024, { noteOn (0, kick) });
            rig.instrument->allNotesOff();
            rig.render (2048);
            expect (rig.peak (0, 1024 + 1024, 1024 + 2048) < 1.0e-4f);
        }
    }

    void testSampler()
    {
        beginTest ("Sampler plays C3 at the original pitch and repitches chromatically");
        {
            auto sample = sineSample (300.0f, 1.0f);
            for (int note : { 60, 72, 48, 67 })
            {
                InstrumentRig rig (InstrumentType::sampler);
                rig.instrument->setSample (0, sample.get());
                rig.render (12000, { noteOn (0, note) });
                const float expected = 300.0f * std::exp2 ((float) (note - 60) / 12.0f);
                const float hz = estimatePitch (rig.dry.getReadPointer (0) + 2000, 8192, testRate);
                expectWithinAbsoluteError (hz, expected, expected * 0.01f);
            }
        }

        beginTest ("Sampler voice ends when the sample runs out; note 72 plays twice as fast");
        {
            auto sample = constantSample (9600);
            InstrumentRig root (InstrumentType::sampler), octave (InstrumentType::sampler);
            root.instrument->setSample (0, sample.get());
            octave.instrument->setSample (0, sample.get());
            root.render (12000, { noteOn (0, 60) });
            octave.render (12000, { noteOn (0, 72) });
            expect (std::abs (root.lastAudible() - 9600) < 100);
            expect (std::abs (octave.lastAudible() - 4800) < 100);
        }

        beginTest ("Sampler Gate releases at note-off, Trig ignores it");
        {
            auto sample = constantSample (48000);
            const auto& spec = getInstrumentSpec (InstrumentType::sampler);
            for (int mode : { 0, 1 })
            {
                InstrumentRig rig (InstrumentType::sampler);
                auto values = spec.defaults();
                values[(size_t) spec.indexOf ("envMode")] = (float) mode;
                values[(size_t) spec.indexOf ("release")] = 0.05f;
                rig.instrument->setParameters (values.data(), (int) values.size());
                rig.instrument->setSample (0, sample.get());
                rig.render (24000, { noteOn (0, 60), noteOff (4800, 60) });
                expect (rig.peak (0, 4000, 4800) > 0.2f);
                if (mode == 1)
                    expect (rig.peak (0, 4800 + 4800, 24000) < 1.0e-3f, "Gate voice kept sounding");
                else
                    expect (rig.peak (0, 4800 + 4800, 24000) > 0.2f, "Trig voice stopped at note-off");
            }
        }

        beginTest ("Sampler polyphony, filter LFO and sample swaps stay well-behaved");
        {
            auto sample = FactorySounds::render ("tonal-pad");
            const auto& spec = getInstrumentSpec (InstrumentType::sampler);
            InstrumentRig rig (InstrumentType::sampler);
            auto values = spec.defaults();
            values[(size_t) spec.indexOf ("cutoff")] = 800.0f;
            values[(size_t) spec.indexOf ("resonance")] = 0.6f;
            values[(size_t) spec.indexOf ("filterLfo")] = 1.0f;
            values[(size_t) spec.indexOf ("lfoRate")] = 8.0f;
            rig.instrument->setParameters (values.data(), (int) values.size());
            rig.instrument->setSample (0, sample.get());

            std::vector<NoteEvent> events;
            for (int i = 0; i < 24; ++i)
                events.push_back (noteOn (i * 64, 48 + i, 0.8f));
            rig.render (24000, events);
            bool finite = true;
            for (int i = 0; i < rig.dry.getNumSamples(); ++i)
                finite = finite && std::isfinite (rig.dry.getSample (0, i));
            expect (finite);
            expect (rig.peak (0, 0, 24000) > 0.05f);

            auto other = sineSample (200.0f, 1.0f);
            rig.instrument->setSample (0, other.get());
            sample.reset();
            rig.render (4800);
            expect (rig.peak (0, 24000 + 2048, 24000 + 4800) < 1.0e-4f);
        }
    }

    void testPresets()
    {
        beginTest ("Drum Kit and Sampler presets reference real sounds and parameters");
        {
            int kits = 0, samplerPresets = 0;
            const auto& padSpec = getPadSpec();
            for (auto& preset : getFactoryPresets())
            {
                if (preset.type == InstrumentType::drumKit)
                {
                    ++kits;
                    expectEquals (preset.category, juce::String ("Drums"));
                    for (auto& [id, v] : preset.values)
                        expect (getInstrumentSpec (InstrumentType::drumKit).indexOf (id) >= 0, preset.name + ": " + id);
                    for (auto& pad : preset.pads)
                    {
                        expect (pad.name.isNotEmpty(), preset.name + ": unnamed pad");
                        expect (pad.sampleRef.startsWith ("factory:"), preset.name + ": " + pad.sampleRef);
                        expect (FactorySounds::render (pad.sampleRef.fromFirstOccurrenceOf (":", false, false)) != nullptr,
                                preset.name + ": missing " + pad.sampleRef);
                        for (auto& [id, v] : pad.values)
                        {
                            const int index = padSpec.indexOf (id);
                            expect (index >= 0, preset.name + "/" + pad.name + ": unknown " + id);
                            if (index >= 0)
                                expectEquals (padSpec.params[(size_t) index].clamp (v), v);
                        }
                    }
                    for (auto& fx : preset.effects)
                        expect (fx.type != EffectType::none && fx.name.isNotEmpty(), preset.name + ": effect slot empty");

                    if (preset.name != "Hand Percussion")
                    {
                        // Standard layout: hats choke each other.
                        for (int p : { 4, 5 })
                        {
                            auto it = preset.pads[(size_t) p].values.find ("choke");
                            expect (it != preset.pads[(size_t) p].values.end() && it->second > 0.5f, preset.name + ": hat without choke");
                        }
                        expect (preset.pads[0].sampleRef.contains ("kick"), preset.name + ": pad 0 is not a kick");
                    }
                }
                else if (preset.type == InstrumentType::sampler)
                {
                    samplerPresets += preset.category == "Sampler" ? 1 : 0;
                    expect (FactorySounds::render (preset.sampleRef.fromFirstOccurrenceOf ("factory:", false, false)) != nullptr,
                            preset.name + ": missing " + preset.sampleRef);
                    const auto& spec = getInstrumentSpec (InstrumentType::sampler);
                    for (auto& [id, v] : preset.values)
                    {
                        const int index = spec.indexOf (id);
                        expect (index >= 0, preset.name + ": unknown " + id);
                        if (index >= 0)
                            expectEquals (spec.params[(size_t) index].clamp (v), v);
                    }
                    expect (getPresetCategories().contains (preset.category));
                }
            }
            expect (kits >= 8);
            expect (samplerPresets >= 10);
        }
    }

    void testThroughEngine()
    {
        beginTest ("A drum kit preset and a sampler preset sound through the session engine");
        {
            for (auto* presetName : { "Boom Kit", "Tine Keys" })
            {
                auto* preset = findFactoryPreset (presetName);
                expect (preset != nullptr, presetName);
                if (preset == nullptr)
                    continue;

                Rig rig;
                Set set;
                addTrack (set, *preset);
                rig.open (set);
                const int pitch = preset->type == InstrumentType::drumKit ? drumBaseNote : samplerRootNote;
                rig.post (Command::Type::noteOn, set.tracks[0].id, 0, pitch, 1.0f);
                float peak = 0.0f;
                for (int b = 0; b < 20; ++b)
                {
                    rig.render (Rig::blockSize);
                    peak = juce::jmax (peak, rig.buffer.getMagnitude (0, 0, Rig::blockSize));
                }
                expect (peak > 0.05f, juce::String (presetName) + " is silent in the engine");
            }
        }
    }
};

static DrumSamplerTests drumSamplerTests;
