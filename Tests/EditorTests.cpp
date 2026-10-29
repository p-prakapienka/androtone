#include "TestHelpers.h"

using namespace rb;
using namespace rbtest;

// Pure helpers behind the MIDI editor (model/EditorOps.h).
class EditorTests : public juce::UnitTest
{
public:
    EditorTests() : juce::UnitTest ("Editor", "model") {}

    void runTest() override
    {
        beginTest ("Grid snapping");
        {
            expectWithinAbsoluteError (snapToGrid (0.37, 0.25), 0.25, 1.0e-9);
            expectWithinAbsoluteError (snapToGrid (0.38, 0.25), 0.5, 1.0e-9);
            expectWithinAbsoluteError (snapToGrid (0.38, 0.0), 0.38, 1.0e-9);
            expectWithinAbsoluteError (floorToGrid (0.49, 0.25), 0.25, 1.0e-9);
            // Values a hair below a grid line (floating point) still floor onto it.
            expectWithinAbsoluteError (floorToGrid (0.75 - 1.0e-9, 0.25), 0.75, 1.0e-9);
            expectWithinAbsoluteError (floorToGrid (1.0, tripletOf (0.5)), 1.0, 1.0e-9);
        }

        beginTest ("Adaptive grid follows zoom (EDIT-4)");
        {
            expectWithinAbsoluteError (editorGridForZoom (90.0, false), 0.25, 1.0e-9);   // 1 bar on ~360 px
            expectWithinAbsoluteError (editorGridForZoom (45.0, false), 0.5, 1.0e-9);    // 2 bars
            expectWithinAbsoluteError (editorGridForZoom (400.0, false), 0.125, 1.0e-9); // zoomed in
            expectWithinAbsoluteError (editorGridForZoom (5.0, false), 1.0, 1.0e-9);     // never coarser than 1/4
            expectWithinAbsoluteError (editorGridForZoom (90.0, true), tripletOf (0.5), 1.0e-9);
        }

        beginTest ("Rows: drum pads, chromatic and in-key ranges (EDIT-2)");
        {
            auto drums = editorRows (true, false, 0, 0);
            expectEquals ((int) drums.size(), numDrumPads);
            expectEquals (drums.front(), drumBaseNote);

            expectEquals ((int) editorRows (false, false, 0, 0).size(), 128);
            auto major = editorRows (false, true, 0, 0);
            expect (rowIndexOf (major, 60) >= 0);
            expectEquals (rowIndexOf (major, 61), -1);
            expectEquals (major[(size_t) rowIndexOf (major, 64) + 1], 65);

            auto clip = clipWithNotes ({ note (0, 64), note (1, 60), note (2, 64) });
            auto rows = previewRows (clip->notes, { note (3, 67) }, 0.0, 4.0, false, 0, false, 0, 0);
            expectEquals ((int) rows.size(), 3);
            expectEquals (rows[0], 60);
            expectEquals (rows[2], 67);

            // Notes outside the window are left out; drums always show the selected pad.
            auto drumRows = previewRows ({ note (0, drumBaseNote), note (5, drumBaseNote + 3) }, {}, 0.0, 4.0, true, 2,
                                         false, 0, 0);
            expectEquals ((int) drumRows.size(), 2);
            expectEquals (drumRows[1], drumBaseNote + 2);

            auto empty = previewRows ({}, {}, 0.0, 4.0, false, 0, true, 0, 0);
            expectEquals (empty.front(), 48);
            expectEquals (empty.back(), 72);
        }

        beginTest ("Hit testing notes and their right edge (EDIT-5, EDIT-7)");
        {
            auto clip = clipWithNotes ({ note (0.0, 60, 1.0), note (2.0, 60, 0.25), note (0.5, 62, 0.5) });

            auto body = hitTestNote (*clip, 0.3, 60, 0.1);
            expectEquals (body.index, 0);
            expect (! body.rightEdge);

            auto edge = hitTestNote (*clip, 0.95, 60, 0.1);
            expectEquals (edge.index, 0);
            expect (edge.rightEdge);

            auto past = hitTestNote (*clip, 1.05, 60, 0.1); // just after the end still grabs the edge
            expectEquals (past.index, 0);
            expect (past.rightEdge);

            expectEquals (hitTestNote (*clip, 1.5, 60, 0.1).index, -1);
            expectEquals (hitTestNote (*clip, 0.3, 61, 0.1).index, -1);

            // A short note keeps its first half for moving even with a wide edge zone.
            auto shortNote = hitTestNote (*clip, 2.05, 60, 0.5);
            expect (shortNote.index >= 0 && ! shortNote.rightEdge);
            expect (hitTestNote (*clip, 2.2, 60, 0.5).rightEdge);
        }

        beginTest ("Drawing a drum run and a melodic note (EDIT-6)");
        {
            auto run = makeDrumRun (drumBaseNote, 0.3, 1.1, 0.25, 0.9f, 4.0);
            expectEquals ((int) run.size(), 4); // cells at 0.25, 0.5, 0.75, 1.0
            expectWithinAbsoluteError (run.front().start, 0.25, 1.0e-9);
            expectWithinAbsoluteError (run.back().start, 1.0, 1.0e-9);
            expectWithinAbsoluteError (run.back().length, 0.25, 1.0e-9);
            expectEquals ((int) makeDrumRun (drumBaseNote, 1.1, 0.3, 0.25, 0.9f, 4.0).size(), 4); // backwards
            expectEquals ((int) makeDrumRun (drumBaseNote, 3.5, 9.0, 0.25, 0.9f, 4.0).size(), 2); // clipped to clip

            auto drawn = makeDrawnNote (60, 1.1, 2.3, 0.25, 0.8f, 8.0);
            expectWithinAbsoluteError (drawn.start, 1.0, 1.0e-9);
            expectWithinAbsoluteError (drawn.length, 1.5, 1.0e-9);
            auto tiny = makeDrawnNote (60, 1.1, 1.1, 0.5, 0.8f, 8.0);
            expectWithinAbsoluteError (tiny.length, 0.5, 1.0e-9);
            auto backwards = makeDrawnNote (60, 2.3, 1.1, 0.25, 0.8f, 8.0);
            expectWithinAbsoluteError (backwards.start, 1.0, 1.0e-9);
            auto atEnd = makeDrawnNote (60, 3.9, 7.0, 0.25, 0.8f, 4.0);
            expectWithinAbsoluteError (atEnd.start + atEnd.length, 4.0, 1.0e-9);
        }

        beginTest ("addNotes skips duplicates and selects what was added");
        {
            auto clip = clipWithNotes ({ note (0.5, drumBaseNote) });
            auto result = addNotes (*clip, makeDrumRun (drumBaseNote, 0.0, 0.75, 0.25, 0.9f, 4.0));
            expectEquals ((int) result.clip->notes.size(), 4);
            expectEquals ((int) result.selection.size(), 3);
            for (auto i : result.selection)
                expect (std::abs (result.clip->notes[(size_t) i].start - 0.5) > 0.01);
        }

        beginTest ("Velocity ramp and swipe drawing (EDIT-10)");
        {
            auto ramp = velocityRamp (5, 0.2f, 1.0f);
            expectEquals ((int) ramp.size(), 5);
            expectWithinAbsoluteError (ramp[0], 0.2f, 1.0e-6f);
            expectWithinAbsoluteError (ramp[2], 0.6f, 1.0e-6f);
            expectWithinAbsoluteError (ramp[4], 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (velocityRamp (1, 0.3f, 0.9f)[0], 0.3f, 1.0e-6f);

            std::vector<float> values (6, 0.5f);
            drawValues (values, 4, 1.0f, 1, 0.4f); // right to left
            expectWithinAbsoluteError (values[0], 0.5f, 1.0e-6f);
            expectWithinAbsoluteError (values[1], 0.4f, 1.0e-6f);
            expectWithinAbsoluteError (values[4], 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (values[5], 0.5f, 1.0e-6f);
            drawValues (values, 9, 0.1f, 9, 0.1f); // out of range is ignored
            expectWithinAbsoluteError (values[5], 0.5f, 1.0e-6f);
        }

        beginTest ("Loop brace: tapping bars adds and removes them (EDIT-12)");
        {
            auto clip = std::make_shared<Clip> (*makeEmptyClip (4)); // loop = bars 0..3
            auto last = toggleLoopBar (*clip, 3);
            expectWithinAbsoluteError (last.start, 0.0, 1.0e-9);
            expectWithinAbsoluteError (last.length, 12.0, 1.0e-9);

            auto first = toggleLoopBar (*clip, 0);
            expectWithinAbsoluteError (first.start, 4.0, 1.0e-9);
            expectWithinAbsoluteError (first.length, 12.0, 1.0e-9);

            auto middle = toggleLoopBar (*clip, 1);
            expectWithinAbsoluteError (middle.start, 0.0, 1.0e-9);
            expectWithinAbsoluteError (middle.length, 8.0, 1.0e-9);

            clip->loopStart = 4.0;
            clip->loopLength = 4.0;
            auto single = toggleLoopBar (*clip, 1); // the only bar stays
            expectWithinAbsoluteError (single.length, 4.0, 1.0e-9);
            auto right = toggleLoopBar (*clip, 3);
            expectWithinAbsoluteError (right.start, 4.0, 1.0e-9);
            expectWithinAbsoluteError (right.length, 12.0, 1.0e-9);
            auto left = toggleLoopBar (*clip, 0);
            expectWithinAbsoluteError (left.start, 0.0, 1.0e-9);
            expectWithinAbsoluteError (left.length, 8.0, 1.0e-9);
        }

        beginTest ("Loop brace: dragging edges snaps to beats");
        {
            auto clip = makeEmptyClip (2);
            auto end = dragLoopEdge (*clip, true, 5.4);
            expectWithinAbsoluteError (end.length, 5.0, 1.0e-9);
            auto tooShort = dragLoopEdge (*clip, true, -3.0);
            expectWithinAbsoluteError (tooShort.length, 1.0, 1.0e-9);
            auto start = dragLoopEdge (*clip, false, 2.6);
            expectWithinAbsoluteError (start.start, 3.0, 1.0e-9);
            expectWithinAbsoluteError (start.length, 5.0, 1.0e-9);
            auto past = dragLoopEdge (*clip, true, 100.0);
            expectWithinAbsoluteError (past.start + past.length, 64.0, 1.0e-9);

            // setLoop then grows the clip to hold the loop.
            auto grown = setLoop (*clip, end.start, 12.0);
            expectEquals (grown->lengthBars, 3);
        }

        beginTest ("Notes in loop are in time order");
        {
            auto clip = std::make_shared<Clip> (*clipWithNotes ({ note (0, 60), note (4.5, 62), note (5, 61), note (9, 60) }, 3));
            clip->loopStart = 4.0;
            clip->loopLength = 4.0;
            auto inLoop = notesInLoop (*clip);
            expectEquals ((int) inLoop.size(), 2);
            expectEquals (clip->notes[(size_t) inLoop[0]].pitch, 62);
            expectEquals (clip->notes[(size_t) inLoop[1]].pitch, 61);
        }
    }
};

static EditorTests editorTests;
