#include "TestHelpers.h"

using namespace rb;
using namespace rbtest;

class CaptureTests : public juce::UnitTest
{
public:
    CaptureTests() : juce::UnitTest ("Capture", "capture") {}

    static CaptureEvent ev (int pitch, float velocity, double seconds, bool running = false, double beat = 0.0,
                            int slot = -1, double clipBeat = 0.0)
    {
        CaptureEvent e;
        e.trackId = 1;
        e.pitch = pitch;
        e.velocity = velocity;
        e.timeSeconds = seconds;
        e.transportRunning = running;
        e.songBeat = beat;
        e.playingSlot = slot;
        e.clipBeat = clipBeat;
        return e;
    }

    void runTest() override
    {
        beginTest ("Tempo estimate from a steady pulse");
        {
            // Quarter notes at 120 BPM: one bar of four hits.
            auto estimate = estimateTempo ({ 10.0, 10.5, 11.0, 11.5 });
            expectWithinAbsoluteError (estimate.bpm, 120.0, 1.0);
            expectEquals (estimate.bars, 1);

            // Eighth notes at 96 BPM over two bars.
            std::vector<double> onsets;
            for (int i = 0; i < 16; ++i)
                onsets.push_back (i * 60.0 / 96.0 / 2.0);
            estimate = estimateTempo (onsets);
            expectWithinAbsoluteError (estimate.bpm, 96.0, 1.5);
        }

        beginTest ("Free capture with the transport stopped sets tempo and length (CAP-3)");
        {
            CaptureBuffer buffer;
            for (int i = 0; i < 8; ++i)
            {
                buffer.add (ev (60 + i, 0.8f, 5.0 + i * 0.25));
                buffer.add (ev (60 + i, 0.0f, 5.1 + i * 0.25));
            }

            auto result = buffer.captureFree (1, 100.0, true, 0);
            expect (result.clip != nullptr);
            expectWithinAbsoluteError (result.tempo, 120.0, 1.0);
            expectEquals (result.clip->lengthBars, 1);
            expectEquals ((int) result.clip->notes.size(), 8);
            expectWithinAbsoluteError (result.clip->notes[1].start, 0.5, 0.01);
            expectWithinAbsoluteError (result.clip->notes[0].length, 0.2, 0.01);

            // Without tempo detection the set tempo is used.
            auto fixed = buffer.captureFree (1, 60.0, false, 0);
            expectEquals (fixed.tempo, 0.0);
            expectWithinAbsoluteError (fixed.clip->notes[1].start, 0.25, 0.01);
        }

        beginTest ("Free capture while running aligns to bars and respects the shortest clip (CAP-4)");
        {
            CaptureBuffer buffer;
            buffer.add (ev (36, 0.9f, 0, true, 9.0));
            buffer.add (ev (36, 0.0f, 0, true, 9.25));
            buffer.add (ev (38, 0.9f, 0, true, 10.0));
            buffer.add (ev (38, 0.0f, 0, true, 10.5));

            auto result = buffer.captureFree (1, 120.0, false, 2);
            expectEquals (result.clip->lengthBars, 2);
            expectWithinAbsoluteError (result.clip->notes[0].start, 1.0, 1.0e-9);
            expectWithinAbsoluteError (result.clip->notes[1].start, 2.0, 1.0e-9);
        }

        beginTest ("Notes played over a playing clip become pending overdubs (CAP-5)");
        {
            CaptureBuffer buffer;
            buffer.add (ev (60, 0.7f, 0, true, 5.5, 2, 3.5));
            buffer.add (ev (60, 0.0f, 0, true, 6.5, 2, 0.5)); // wrapped around the loop
            buffer.add (ev (62, 0.7f, 0, true, 6.0, -1));      // free note, not for the clip

            auto clip = clipWithNotes ({ note (0, 48) });
            auto pending = buffer.pendingForClip (1, 2, *clip);
            expectEquals ((int) pending.size(), 1);
            expectWithinAbsoluteError (pending[0].start, 3.5, 1.0e-9);
            expectWithinAbsoluteError (pending[0].length, 1.0, 1.0e-9);

            auto merged = mergeNotes (*clip, pending);
            expectEquals ((int) merged->notes.size(), 2);

            buffer.clearPendingForClip (1, 2);
            expect (buffer.pendingForClip (1, 2, *clip).empty());
            expect (buffer.hasFreeNotes (1));
        }

        beginTest ("Capture timeout clears the free buffer after a pause (CAP-7)");
        {
            CaptureBuffer buffer;
            buffer.add (ev (60, 0.7f, 1.0));
            buffer.add (ev (60, 0.0f, 1.2));
            buffer.update (3.0, true);
            expect (buffer.hasFreeNotes (1));
            buffer.update (6.0, false);
            expect (buffer.hasFreeNotes (1));
            buffer.update (6.0, true);
            expect (! buffer.hasFreeNotes (1));
        }
    }
};

static CaptureTests captureTests;
