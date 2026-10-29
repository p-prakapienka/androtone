#include "TestHelpers.h"

using namespace rb;
using namespace rbtest;

class ModelTests : public juce::UnitTest
{
public:
    ModelTests() : juce::UnitTest ("Model", "model") {}

    void runTest() override
    {
        beginTest ("Scales and transposition");
        {
            expect (isInScale (60, 0, 0));
            expect (! isInScale (61, 0, 0));
            expectEquals (transposeInScale (64, 1, true, 0, 0), 65);  // E -> F in C major
            expectEquals (transposeInScale (65, -1, true, 0, 0), 64);
            expectEquals (transposeInScale (64, 1, false, 0, 0), 65);
            expectEquals (transposeInScale (71, 1, true, 0, 0), 72);  // B -> C
            expectEquals (noteName (60), juce::String ("C3"));
            expectEquals (snapToScale (61, 0, 0), 60);
        }

        beginTest ("Parameter normalisation round-trips with skew");
        {
            auto p = ParamSpec::continuous ("f", "Freq", 20.0f, 20000.0f, 1000.0f, ParamUnit::hertz, 1000.0f);
            expectWithinAbsoluteError (p.toNormalised (1000.0f), 0.5f, 0.001f);
            expectWithinAbsoluteError (p.fromNormalised (p.toNormalised (440.0f)), 440.0f, 0.5f);
            auto c = ParamSpec::choice ("t", "Type", { "A", "B", "C" }, 1);
            expectEquals (c.fromNormalised (0.9f), 2.0f);
            expectEquals (c.format (1.0f), juce::String ("B"));
        }

        beginTest ("Volume slider maps 0.8 to unity");
        {
            expectWithinAbsoluteError (volumeToGain (0.8f), 1.0f, 0.001f);
            expectWithinAbsoluteError (volumeToDecibels (1.0f), 6.0f, 0.001f);
            expectEquals (volumeToGain (0.0f), 0.0f);
        }

        beginTest ("Tracks: add up to eight, duplicate, move, delete (TRK)");
        {
            Set set;
            for (int i = 0; i < 9; ++i)
                addTrack (set, simplePreset());
            expectEquals ((int) set.tracks.size(), 8);

            Set small;
            addTrack (small, simplePreset());
            addTrack (small, simplePreset());
            small.tracks[0].slots[1] = clipWithNotes ({ note (0, 60) });
            const int copy = duplicateTrack (small, small.tracks[0].id);
            expectEquals ((int) small.tracks.size(), 3);
            expectEquals (small.tracks[1].id, copy);
            expect (small.tracks[1].slots[1] != nullptr);

            moveTrack (small, copy, 2);
            expectEquals (small.tracks[2].id, copy);
            deleteTrack (small, copy);
            expectEquals ((int) small.tracks.size(), 2);
            expect (small.tracks[0].id != small.tracks[1].id);
        }

        beginTest ("Clips and scenes: duplicate, move, delete (CLIP-4, SCN-3)");
        {
            Set set;
            addTrack (set, simplePreset());
            addTrack (set, simplePreset());
            const int a = set.tracks[0].id, b = set.tracks[1].id;
            set.tracks[0].slots[0] = clipWithNotes ({ note (0, 60) });

            expectEquals (duplicateClip (set, a, 0), 1);
            moveClip (set, a, 1, b, 3);
            expect (set.tracks[0].slots[1] == nullptr);
            expect (set.tracks[1].slots[3] != nullptr);

            expect (duplicateScene (set, 0));
            expect (set.tracks[0].slots[1] != nullptr);
            expect (set.tracks[1].slots[4] != nullptr); // pushed down

            moveScene (set, 4, 0);
            expect (set.tracks[1].slots[0] != nullptr);

            deleteScene (set, 0);
            expect (set.tracks[1].slots[0] == nullptr);
            expect (set.tracks[0].slots[0] != nullptr);
        }

        beginTest ("Note editing: move, resize, transpose, quantize and restore (EDIT-10)");
        {
            auto clip = clipWithNotes ({ note (0.1, 60), note (1.05, 62), note (2.0, 64) });
            auto moved = moveNotes (*clip, { 0 }, 2.5, 1);
            expectEquals ((int) moved.selection.size(), 1);
            expectEquals (moved.clip->notes[(size_t) moved.selection[0]].pitch, 61);
            expectWithinAbsoluteError (moved.clip->notes[(size_t) moved.selection[0]].start, 2.6, 1.0e-9);

            auto quantized = quantizeNotes (*clip, selectAll (*clip), 0.25, 1.0f);
            expectWithinAbsoluteError (quantized.clip->notes[0].start, 0.0, 1.0e-9);
            expectWithinAbsoluteError (quantized.clip->notes[1].start, 1.0, 1.0e-9);

            auto restored = quantizeNotes (*quantized.clip, selectAll (*quantized.clip), 0.0, 1.0f);
            expectWithinAbsoluteError (restored.clip->notes[0].start, 0.1, 1.0e-9);

            auto transposed = transposeNotes (*clip, { 2 }, 1, false, true, 0, 0);
            expectEquals (transposed.clip->notes[2].pitch, 65);

            auto drums = clipWithNotes ({ note (0, drumBaseNote) });
            expectEquals (transposeNotes (*drums, { 0 }, -1, true, false, 0, 0).clip->notes[0].pitch, drumBaseNote);

            auto resized = resizeNotes (*clip, { 0 }, 0.5, 0.0625);
            expectWithinAbsoluteError (resized.clip->notes[0].length, 0.75, 1.0e-9);
        }

        beginTest ("Loop brace: duplicate, crop, delete content (EDIT-12)");
        {
            auto clip = clipWithNotes ({ note (0, 60), note (1, 61) });
            auto doubled = duplicateLoop (*clip);
            expectEquals (doubled->lengthBars, 2);
            expectEquals (doubled->loopLength, 8.0);
            expectEquals ((int) doubled->notes.size(), 4);
            expectEquals (doubled->notes[2].start, 4.0);

            auto looped = setLoop (*doubled, 4.0, 4.0);
            auto cropped = cropToLoop (*looped);
            expectEquals (cropped->lengthBars, 1);
            expectEquals ((int) cropped->notes.size(), 2);
            expectEquals (cropped->notes[0].start, 0.0);

            auto cleared = deleteLoopContent (*looped);
            expectEquals ((int) cleared->notes.size(), 2);
            expectEquals (cleared->notes[1].start, 1.0);
        }

        beginTest ("Set JSON round trip");
        {
            Set set;
            set.name = "Round Trip";
            set.tempo = 97.5;
            set.inKey = true;
            set.root = 9;
            set.scale = 1;
            addTrack (set, simplePreset (InstrumentType::drumKit));
            addTrack (set, simplePreset (InstrumentType::analog));
            set.tracks[1].volume = 0.5f;
            set.tracks[1].mute = true;
            set.tracks[1].perf.repeat = 3;

            auto clip = std::make_shared<Clip> (*clipWithNotes ({ note (0.5, 60, 1.0, 0.25f) }, 2));
            AutomationLane lane;
            lane.target.device = ParamTarget::instrument;
            lane.target.paramIndex = 0;
            lane.points = { { 0.0, -6.0f }, { 4.0, 0.0f } };
            clip->automation.push_back (lane);
            set.tracks[1].slots[2] = clip;

            const auto json = juce::JSON::toString (setToVar (set));
            const auto loaded = setFromVar (juce::JSON::parse (json));

            expectEquals (loaded.name, set.name);
            expectEquals (loaded.tempo, 97.5);
            expect (loaded.inKey);
            expectEquals (loaded.root, 9);
            expectEquals ((int) loaded.tracks.size(), 2);
            expect (loaded.tracks[0].isDrum());
            expectEquals (loaded.tracks[1].volume, 0.5f);
            expect (loaded.tracks[1].mute);
            expectEquals (loaded.tracks[1].perf.repeat, 3);

            auto& loadedClip = loaded.tracks[1].slots[2];
            expect (loadedClip != nullptr);
            expectEquals (loadedClip->lengthBars, 2);
            expect (loadedClip->notes[0] == clip->notes[0]);
            expectEquals ((int) loadedClip->automation.size(), 1);
            expectEquals (loadedClip->automation[0].valueAt (2.0), -3.0f);
            expectEquals (loaded.nextTrackId, set.nextTrackId);
        }

        beginTest ("Drum kit Pad FX is fully wet; type-only effect presets get factory values");
        {
            for (auto& preset : getFactoryPresets())
            {
                Track t;
                applyPreset (t, preset);
                if (t.isDrum() && t.effects[0].type != (int) EffectType::none)
                {
                    const int mix = getEffectSpec ((EffectType) t.effects[0].type).indexOf ("mix");
                    if (mix >= 0)
                        expectEquals (t.effects[0].values[(size_t) mix], 1.0f, preset.name);
                }
                for (auto& fx : t.effects)
                    if (fx.type != (int) EffectType::none)
                        expect (fx.presetName.isNotEmpty(), preset.name);
            }
        }

        beginTest ("Undo and redo restore whole set states (GLB-1)");
        {
            SessionProcessor session;
            SampleLibrary library;
            Document doc (session, library);
            Set set;
            addTrack (set, simplePreset());
            doc.openInMemory (set);

            doc.edit ([] (Set& s) { s.tempo = 90.0; });
            doc.beginGesture();
            doc.edit ([] (Set& s) { s.tracks[0].volume = 0.1f; });
            doc.edit ([] (Set& s) { s.tracks[0].volume = 0.2f; });
            doc.endGesture();

            expectEquals (doc.get().tracks[0].volume, 0.2f);
            doc.undo();
            expectEquals (doc.get().tracks[0].volume, 0.8f);
            expectEquals (doc.get().tempo, 90.0);
            doc.undo();
            expectEquals (doc.get().tempo, 120.0);
            expect (! doc.canUndo());
            doc.redo();
            expectEquals (doc.get().tempo, 90.0);
        }

        beginTest ("Documents save to disk and reopen");
        {
            auto folder = juce::File::createTempFile ("rbset");
            {
                SessionProcessor session;
                SampleLibrary library;
                Document doc (session, library);
                Set set;
                set.name = "Saved";
                addTrack (set, simplePreset());
                doc.open (folder, set);
                doc.edit ([] (Set& s) { s.tempo = 133.0; });
                doc.saveNow();
            }
            {
                SessionProcessor session;
                SampleLibrary library;
                Document doc (session, library);
                doc.open (folder, Set());
                expectEquals (doc.get().name, juce::String ("Saved"));
                expectEquals (doc.get().tempo, 133.0);
            }
            folder.deleteRecursively();
        }
    }
};

static ModelTests modelTests;
