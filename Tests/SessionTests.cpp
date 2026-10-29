#include "TestHelpers.h"

using namespace rb;
using namespace rbtest;

// 120 BPM at 48 kHz: one beat is 24000 samples, one bar 96000.
static constexpr juce::int64 beat = 24000;

class SessionTests : public juce::UnitTest
{
public:
    SessionTests() : juce::UnitTest ("Session", "engine") {}

    Set makeSet (int numTracks)
    {
        Set set;
        set.tempo = 120.0;
        for (int i = 0; i < numTracks; ++i)
            addTrack (set, simplePreset());
        return set;
    }

    void runTest() override
    {
        beginTest ("Launching a clip while stopped starts it immediately and loops it (CLIP-2)");
        {
            ProbeScope probes;
            Rig rig;
            auto set = makeSet (1);
            set.tracks[0].slots[0] = clipWithNotes ({ note (0, 60), note (1, 62), note (2, 64), note (3, 65) });
            rig.open (set);

            rig.post (Command::Type::launchClip, set.tracks[0].id, 0);
            rig.render (beat * 6);

            auto ons = probes.logs.back()->noteOns();
            expectEquals ((int) ons.size(), 6);
            for (int i = 0; i < 6; ++i)
                expectEquals ((juce::int64) ons[(size_t) i].sample, (juce::int64) i * beat);
            expectEquals (ons[4].pitch, 60);
            expect (rig.session.isRunning());
        }

        beginTest ("Launching while running waits for the next downbeat");
        {
            ProbeScope probes;
            Rig rig;
            auto set = makeSet (2);
            set.tracks[0].slots[0] = clipWithNotes ({ note (0, 36) });
            set.tracks[1].slots[0] = clipWithNotes ({ note (0, 72) });
            rig.open (set);

            rig.post (Command::Type::launchClip, set.tracks[0].id, 0);
            rig.render (beat + beat / 3);
            rig.post (Command::Type::launchClip, set.tracks[1].id, 0);
            rig.render (beat * 4);

            auto ons = probes.logs[1]->noteOns();
            expectEquals ((int) ons.size(), 1);
            expectEquals (ons[0].sample, beat * 4);
        }

        beginTest ("Switching clips on a track happens at the bar line");
        {
            ProbeScope probes;
            Rig rig;
            auto set = makeSet (1);
            set.tracks[0].slots[0] = clipWithNotes ({ note (0, 60), note (2, 60), note (3.5, 60) });
            set.tracks[0].slots[1] = clipWithNotes ({ note (0, 50), note (1, 50) });
            rig.open (set);

            rig.post (Command::Type::launchClip, set.tracks[0].id, 0);
            rig.render (beat * 2 + beat / 2);
            rig.post (Command::Type::launchClip, set.tracks[0].id, 1);
            rig.render (beat * 3);

            auto ons = probes.logs.back()->noteOns();
            expectEquals ((int) ons.size(), 5);
            expectEquals (ons[2].sample, beat * 3 + beat / 2);
            expectEquals (ons[2].pitch, 60);
            expectEquals (ons[3].sample, beat * 4);
            expectEquals (ons[3].pitch, 50);
            expectEquals (ons[4].sample, beat * 5);
        }

        beginTest ("Stopping a clip waits for the downbeat (CLIP-3)");
        {
            ProbeScope probes;
            Rig rig;
            auto set = makeSet (1);
            set.tracks[0].slots[0] = clipWithNotes ({ note (0, 60), note (1, 60), note (2, 60), note (3, 60) });
            rig.open (set);

            rig.post (Command::Type::launchClip, set.tracks[0].id, 0);
            rig.render (beat / 2);
            rig.post (Command::Type::stopClip, set.tracks[0].id, 0);
            rig.render (beat * 8);

            expectEquals ((int) probes.logs.back()->noteOns().size(), 4);
            expect (rig.session.findTrackState (set.tracks[0].id)->playingSlot.load() == -1);
        }

        beginTest ("Note-offs follow note length");
        {
            ProbeScope probes;
            Rig rig;
            auto set = makeSet (1);
            set.tracks[0].slots[0] = clipWithNotes ({ note (1, 60, 0.5) });
            rig.open (set);

            rig.post (Command::Type::launchClip, set.tracks[0].id, 0);
            rig.render (beat * 3);

            auto& entries = probes.logs.back()->entries;
            expectEquals ((int) entries.size(), 2);
            expectEquals (entries[0].sample, beat);
            expectEquals (entries[1].sample, beat + beat / 2);
            expectEquals (entries[1].velocity, 0.0f);
        }

        beginTest ("Scenes launch every clip in the row and stop tracks with empty slots (SCN-2)");
        {
            ProbeScope probes;
            Rig rig;
            auto set = makeSet (2);
            set.tracks[0].slots[0] = clipWithNotes ({ note (0, 1) });
            set.tracks[1].slots[0] = clipWithNotes ({ note (0, 2) });
            set.tracks[0].slots[1] = clipWithNotes ({ note (0, 3) });
            rig.open (set);

            rig.post (Command::Type::launchScene, 0, 0);
            rig.render (beat);
            rig.post (Command::Type::launchScene, 0, 1);
            rig.render (beat * 8);

            auto ons0 = probes.logs[0]->noteOns();
            auto ons1 = probes.logs[1]->noteOns();
            expectEquals ((int) ons1.size(), 1); // track 2 stopped at the bar
            expectEquals ((int) ons0.size(), 3); // 1 at bar 1, 3 at bars 2 and 3
            expectEquals (ons0[1].pitch, 3);
            expectEquals (ons0[1].sample, beat * 4);
        }

        beginTest ("Loop region limits playback");
        {
            ProbeScope probes;
            Rig rig;
            auto set = makeSet (1);
            auto clip = std::make_shared<Clip> (*clipWithNotes ({ note (0, 1), note (1, 2), note (2, 3), note (3, 4) }));
            clip->loopStart = 1.0;
            clip->loopLength = 2.0;
            set.tracks[0].slots[0] = clip;
            rig.open (set);

            rig.post (Command::Type::launchClip, set.tracks[0].id, 0);
            rig.render (beat * 4);

            auto ons = probes.logs.back()->noteOns();
            expectEquals ((int) ons.size(), 4);
            expectEquals (ons[0].pitch, 2);
            expectEquals (ons[1].pitch, 3);
            expectEquals (ons[2].pitch, 2);
            expectEquals (ons[2].sample, beat * 2);
        }

        beginTest ("Stop and play resumes the clips that were playing");
        {
            ProbeScope probes;
            Rig rig;
            auto set = makeSet (2);
            set.tracks[0].slots[2] = clipWithNotes ({ note (0, 5) });
            set.tracks[1].slots[0] = clipWithNotes ({ note (0, 6) });
            rig.open (set);

            rig.post (Command::Type::launchClip, set.tracks[0].id, 2);
            rig.render (beat);
            rig.post (Command::Type::stop);
            rig.render (beat);
            rig.post (Command::Type::play);
            rig.render (beat);

            expectEquals ((int) probes.logs[0]->noteOns().size(), 2);
            expectEquals ((int) probes.logs[1]->noteOns().size(), 0);
        }

        beginTest ("Play with nothing launched starts the first scene with clips");
        {
            ProbeScope probes;
            Rig rig;
            auto set = makeSet (1);
            set.tracks[0].slots[3] = clipWithNotes ({ note (0, 9) });
            rig.open (set);

            rig.post (Command::Type::play);
            rig.render (beat);
            expectEquals ((int) probes.logs[0]->noteOns().size(), 1);
        }

        beginTest ("Live notes play at once; Repeat retriggers on the grid (PAD-7)");
        {
            ProbeScope probes;
            Rig rig;
            auto set = makeSet (1);
            set.tracks[0].perf.repeat = 5; // 1/16
            rig.open (set);

            rig.post (Command::Type::noteOn, set.tracks[0].id, 0, 60, 0.9f);
            rig.render (beat);
            rig.post (Command::Type::noteOff, set.tracks[0].id, 0, 60);
            rig.render (beat);

            auto ons = probes.logs.back()->noteOns();
            expectEquals ((int) ons.size(), 4);
            expectEquals (ons[1].sample, beat / 4);
            expectEquals (ons[3].sample, beat * 3 / 4);

            // Every repeat is captured as a note.
            CaptureEvent events[64];
            const int n = rig.session.readCaptureEvents (events, 64);
            int onCount = 0;
            for (int i = 0; i < n; ++i)
                onCount += events[i].velocity > 0.0f ? 1 : 0;
            expectEquals (onCount, 4);
        }

        beginTest ("Tracks keep their play state when reordered");
        {
            ProbeScope probes;
            Rig rig;
            auto set = makeSet (2);
            set.tracks[0].slots[0] = clipWithNotes ({ note (0, 1), note (2, 1) });
            rig.open (set);
            const int id = set.tracks[0].id;

            rig.post (Command::Type::launchClip, id, 0);
            rig.render (beat);
            rig.document.edit ([&] (Set& s) { moveTrack (s, id, 1); });
            rig.render (beat * 2);

            expectEquals ((int) probes.logs[0]->noteOns().size(), 2);
            expectEquals (rig.session.findTrackState (id)->playingSlot.load(), 0);
        }

        beginTest ("Mute and solo set track gain in the snapshot (MIX-2)");
        {
            ProbeScope probes;
            Rig rig;
            auto set = makeSet (3);
            set.tracks[1].solo = true;
            set.tracks[2].solo = true;
            set.tracks[2].mute = true;
            rig.open (set);

            auto snapshot = rig.session.getPublished();
            expectEquals (snapshot->tracks[0].gain, 0.0f);
            expect (snapshot->tracks[1].gain > 0.9f);
            expectEquals (snapshot->tracks[2].gain, 0.0f);
        }

        beginTest ("Metronome clicks on beats while running (SET-2)");
        {
            ProbeScope probes;
            Rig rig;
            auto set = makeSet (1);
            set.metronome = true;
            set.tracks[0].slots[0] = clipWithNotes ({});
            rig.open (set);

            rig.post (Command::Type::launchClip, set.tracks[0].id, 0);
            rig.render (Rig::blockSize);
            expect (rig.buffer.getMagnitude (0, 0, Rig::blockSize) > 0.05f);
        }

        beginTest ("Duplicate clip switches playback seamlessly (INS-4)");
        {
            ProbeScope probes;
            Rig rig;
            auto set = makeSet (1);
            set.tracks[0].slots[0] = clipWithNotes ({ note (0, 1), note (2, 2) });
            rig.open (set);
            const int id = set.tracks[0].id;

            rig.post (Command::Type::launchClip, id, 0);
            rig.render (beat);
            int newSlot = -1;
            rig.document.edit ([&] (Set& s) { newSlot = duplicateClip (s, id, 0); });
            rig.post (Command::Type::switchClipSeamless, id, newSlot);
            rig.render (beat * 2);

            auto ons = probes.logs[0]->noteOns();
            expectEquals ((int) ons.size(), 2);
            expectEquals (ons[1].sample, beat * 2);
            expectEquals (rig.session.findTrackState (id)->playingSlot.load(), newSlot);
        }

        beginTest ("Offline export renders every scene in order (EXP-1)");
        {
            auto set = makeSet (1);
            set.tracks[0].slots[0] = clipWithNotes ({}, 1);
            set.tracks[0].slots[2] = clipWithNotes ({}, 2);
            SampleLibrary library;
            auto audio = OfflineRenderer::renderScenes (set, {}, library, 48000.0, 1.0);
            expectEquals (audio.getNumSamples(), (int) (beat * 12 + 48000));
        }
    }
};

static SessionTests sessionTests;
