#include "TestHelpers.h"

#include <cstdlib>
#include <new>

// Real-time safety check: counts heap allocations and frees made on the calling thread while
// a RealtimeScope is active. Every audio-thread entry point is exercised inside such a scope
// and must not touch the heap (allocation or deallocation can block on the allocator lock).

namespace
{
    thread_local bool inRealtimeScope = false;
    std::atomic<int> realtimeAllocations { 0 };
    std::atomic<int> realtimeFrees { 0 };

    struct RealtimeScope
    {
        RealtimeScope() { inRealtimeScope = true; }
        ~RealtimeScope() { inRealtimeScope = false; }
    };

    void* countedAlloc (std::size_t size)
    {
        if (inRealtimeScope)
            ++realtimeAllocations;
        if (void* p = std::malloc (size == 0 ? 1 : size))
            return p;
        throw std::bad_alloc();
    }

    void countedFree (void* p) noexcept
    {
        if (p != nullptr && inRealtimeScope)
            ++realtimeFrees;
        std::free (p);
    }
}

void* operator new (std::size_t size) { return countedAlloc (size); }
void* operator new[] (std::size_t size) { return countedAlloc (size); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    if (inRealtimeScope) ++realtimeAllocations;
    return std::malloc (size == 0 ? 1 : size);
}
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept
{
    if (inRealtimeScope) ++realtimeAllocations;
    return std::malloc (size == 0 ? 1 : size);
}
void operator delete (void* p) noexcept { countedFree (p); }
void operator delete[] (void* p) noexcept { countedFree (p); }
void operator delete (void* p, std::size_t) noexcept { countedFree (p); }
void operator delete[] (void* p, std::size_t) noexcept { countedFree (p); }

using namespace rb;

class RealtimeTests : public juce::UnitTest
{
public:
    RealtimeTests() : juce::UnitTest ("Real-time safety", "realtime") {}

    static constexpr double sampleRate = 48000.0;
    static constexpr int blockSize = 192; // an odd-ish size like phones use

    struct Engine
    {
        SampleLibrary samples;
        SessionProcessor session;
        Document document { session, samples };
        juce::AudioBuffer<float> out { 2, blockSize };
        juce::AudioBuffer<float> in { 2, blockSize };

        Engine() { session.prepare (sampleRate, blockSize); }
        ~Engine() { session.releaseResources(); }

        void post (Command::Type type, int trackId = 0, int slot = 0, int pitch = 0, float velocity = 0.0f)
        {
            Command c;
            c.type = type;
            c.trackId = trackId;
            c.slot = slot;
            c.pitch = pitch;
            c.velocity = velocity;
            session.post (c);
        }
    };

    // Renders blocks inside the real-time scope and returns how many heap operations happened.
    int renderChecked (Engine& e, int numBlocks, bool withInput = false)
    {
        // A fake input signal, filled outside the scope.
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
                e.in.setSample (ch, i, 0.1f * std::sin ((float) i * 0.05f));

        const int before = realtimeAllocations.load() + realtimeFrees.load();
        for (int b = 0; b < numBlocks; ++b)
        {
            RealtimeScope scope;
            e.session.process (e.out, blockSize, withInput ? &e.in : nullptr);
        }
        e.session.collectGarbage(); // message thread work, outside the scope
        return realtimeAllocations.load() + realtimeFrees.load() - before;
    }

    void runTest() override
    {
        beginTest ("The allocation counter works");
        {
            const int before = realtimeAllocations.load();
            {
                RealtimeScope scope;
                auto* p = new int (5);
                delete p;
            }
            expectEquals (realtimeAllocations.load() - before, 1);
        }

        beginTest ("Playing every demo set through every scene");
        for (auto& demo : makeDemoSets())
        {
            Engine e;
            e.document.openInMemory (demo);
            int heapOps = 0;
            for (int scene = 0; scene < numScenes; ++scene)
            {
                e.post (Command::Type::launchScene, 0, scene);
                heapOps += renderChecked (e, 600); // ~2.4 s per scene
            }
            expectEquals (heapOps, 0, demo.name);
        }

        beginTest ("Every factory preset: live notes, chords and repeat");
        {
            int presetIndex = 0;
            for (auto& preset : getFactoryPresets())
            {
                Engine e;
                Set set;
                addTrack (set, preset);
                set.tracks[0].perf.repeat = (presetIndex++ % 3 == 0) ? 5 : 0;
                e.document.openInMemory (set);
                const int id = set.tracks[0].id;

                int heapOps = renderChecked (e, 2);
                for (int n = 0; n < 6; ++n)
                    e.post (Command::Type::noteOn, id, 0, (preset.type == InstrumentType::drumKit ? drumBaseNote : 52) + n * 3, 0.9f);
                heapOps += renderChecked (e, 120);
                for (int n = 0; n < 6; ++n)
                    e.post (Command::Type::noteOff, id, 0, (preset.type == InstrumentType::drumKit ? drumBaseNote : 52) + n * 3);
                heapOps += renderChecked (e, 400);

                expectEquals (heapOps, 0, preset.name);
            }
        }

        beginTest ("Every effect type and preset processing a playing track");
        for (auto& fxPreset : getFactoryEffectPresets())
        {
            Engine e;
            Set set;
            addTrack (set, getFactoryPresets().front());
            for (int slot = 0; slot < 2; ++slot)
                applyEffectPreset (set.tracks[0], slot, fxPreset);
            set.tracks[0].slots[0] = rbtest::clipWithNotes ({ rbtest::note (0, drumBaseNote), rbtest::note (1, drumBaseNote + 1),
                                                              rbtest::note (2, drumBaseNote + 4), rbtest::note (3, drumBaseNote + 2) });
            e.document.openInMemory (set);
            e.post (Command::Type::launchClip, set.tracks[0].id, 0);
            expectEquals (renderChecked (e, 300), 0, getEffectDisplayName (fxPreset.type) + " / " + fxPreset.name);
        }

        beginTest ("Edits while playing: parameters, presets, effects, samples, tracks, clips");
        {
            Engine e;
            auto demo = makeDemoSets().front();
            e.document.openInMemory (demo);
            e.post (Command::Type::launchScene, 0, 2);
            int heapOps = renderChecked (e, 50);

            juce::Random random (42);
            const auto& presets = getFactoryPresets();
            const auto& fxPresets = getFactoryEffectPresets();
            const auto& sounds = FactorySounds::list();

            for (int step = 0; step < 120; ++step)
            {
                // Message-thread edits (outside the scope)...
                const auto& set = e.document.get();
                const int trackIndex = random.nextInt ((int) set.tracks.size());
                const int id = set.tracks[(size_t) trackIndex].id;

                switch (step % 8)
                {
                    case 0: // knob moves on every parameter of the instrument
                        e.document.edit ([&] (Set& s)
                        {
                            auto* t = s.findTrack (id);
                            const auto& spec = getInstrumentSpec (t->instrumentType);
                            t->instrument.values = conformValues (spec, t->instrument.values);
                            for (size_t p = 0; p < spec.params.size(); ++p)
                                t->instrument.values[p] = spec.params[p].fromNormalised (random.nextFloat());
                        });
                        break;
                    case 1: // a different preset (may change instrument type)
                        e.document.edit ([&] (Set& s) { applyPreset (*s.findTrack (id), presets[(size_t) random.nextInt ((int) presets.size())]); });
                        break;
                    case 2: // a different effect
                        e.document.edit ([&] (Set& s)
                        {
                            applyEffectPreset (*s.findTrack (id), random.nextInt (2), fxPresets[(size_t) random.nextInt ((int) fxPresets.size())]);
                        });
                        break;
                    case 3: // swap a pad sample or the sampler sample
                        e.document.edit ([&] (Set& s)
                        {
                            auto* t = s.findTrack (id);
                            const auto ref = "factory:" + sounds[(size_t) random.nextInt ((int) sounds.size())].id;
                            if (t->isDrum()) t->pads[(size_t) random.nextInt (numDrumPads)].sampleRef = ref;
                            else t->samplerSampleRef = ref;
                        });
                        break;
                    case 4: // mixer
                        e.document.edit ([&] (Set& s)
                        {
                            auto* t = s.findTrack (id);
                            t->volume = random.nextFloat();
                            t->mute = random.nextBool();
                            t->solo = random.nextInt (5) == 0;
                            t->perf.repeat = random.nextInt ((int) getRepeatRates().size());
                        });
                        break;
                    case 5: // edit the playing clip and launch others
                        e.document.edit ([&] (Set& s)
                        {
                            auto* t = s.findTrack (id);
                            for (auto& clip : t->slots)
                                if (clip != nullptr)
                                    clip = quantizeNotes (*clip, selectAll (*clip), 0.25, 1.0f).clip;
                        });
                        e.post (Command::Type::launchClip, id, random.nextInt (numScenes));
                        break;
                    case 6: // live notes on the track
                        for (int n = 0; n < 4; ++n)
                            e.post (Command::Type::noteOn, id, 0, 40 + random.nextInt (40), random.nextFloat());
                        e.post (Command::Type::pitchBend, id, 0, 0, random.nextFloat() * 2.0f - 1.0f);
                        e.post (Command::Type::pressure, id, 0, 40 + random.nextInt (40), random.nextFloat());
                        e.post (Command::Type::pressure, id, 0, -1, random.nextFloat());
                        e.post (Command::Type::allLiveNotesOff, id);
                        break;
                    case 7: // structural changes
                        if (random.nextBool())
                            e.document.edit ([&] (Set& s) { moveTrack (s, id, random.nextInt ((int) s.tracks.size())); });
                        else
                            e.document.edit ([&] (Set& s) { if (s.tracks.size() < (size_t) maxTracks) duplicateTrack (s, id); else deleteTrack (s, id); });
                        break;
                }

                // ...then audio blocks inside the scope pick them up.
                heapOps += renderChecked (e, 25);
            }

            e.post (Command::Type::stop);
            heapOps += renderChecked (e, 50);
            e.post (Command::Type::play);
            heapOps += renderChecked (e, 50);

            expectEquals (heapOps, 0);
        }

        beginTest ("Recording from input and output, with monitoring");
        {
            Engine e;
            e.document.openInMemory (makeDemoSets().front());
            e.post (Command::Type::launchScene, 0, 0);
            e.session.setMonitorInput (true);

            e.session.startRecording (SessionProcessor::RecordSource::input, 2.0); // allocates here, not in process
            int heapOps = renderChecked (e, 200, true);
            e.session.stopRecording();

            e.session.startRecording (SessionProcessor::RecordSource::output, 2.0);
            heapOps += renderChecked (e, 200, true);
            e.session.stopRecording();

            expectEquals (heapOps, 0);
        }

        beginTest ("Garbage is never freed on the audio thread");
        {
            // Publish many snapshots back to back without the message thread collecting in
            // between: the audio thread must only ever switch pointers.
            Engine e;
            e.document.openInMemory (makeDemoSets().front());
            e.post (Command::Type::launchScene, 0, 0);
            int heapOps = 0;
            for (int i = 0; i < 50; ++i)
            {
                e.document.edit ([i] (Set& s) { s.tempo = 90.0 + i; });
                RealtimeScope scope;
                e.session.process (e.out, blockSize);
                heapOps += 0;
            }
            heapOps = realtimeFrees.load(); // checked cumulatively below
            juce::ignoreUnused (heapOps);
            expectEquals (renderChecked (e, 10), 0);
        }

        beginTest ("No heap traffic at all in any audio-thread scope during this test run");
        {
            // Earlier sections assert per section; this catches anything the per-section
            // sums could miss (e.g. the unchecked loop above).
            logMessage ("allocations " + juce::String (realtimeAllocations.load() - 1) + ", frees "
                        + juce::String (realtimeFrees.load() - 1));
            expectEquals (realtimeAllocations.load(), 1); // the deliberate one in the first section
            expectEquals (realtimeFrees.load(), 1);
        }
    }
};

static RealtimeTests realtimeTests;
