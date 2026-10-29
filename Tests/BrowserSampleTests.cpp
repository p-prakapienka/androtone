#include <rb_engine/rb_engine.h>

using namespace rb;

class BrowserSampleTests : public juce::UnitTest
{
public:
    BrowserSampleTests() : juce::UnitTest ("Browser and samples", "samples") {}

    static SamplePtr makeSample (std::vector<float> left, std::vector<float> right = {})
    {
        auto s = std::make_shared<SampleData>();
        s->buffer.setSize (right.empty() ? 1 : 2, (int) left.size());
        for (int i = 0; i < (int) left.size(); ++i)
        {
            s->buffer.setSample (0, i, left[(size_t) i]);
            if (! right.empty())
                s->buffer.setSample (1, i, right[(size_t) i]);
        }
        return s;
    }

    static float peakOf (const juce::AudioBuffer<float>& b)
    {
        float p = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            p = juce::jmax (p, b.getMagnitude (ch, 0, b.getNumSamples()));
        return p;
    }

    void runTest() override
    {
        beginTest ("Waveform peaks: zoomed out columns match a direct scan");
        {
            juce::Random rng (42);
            std::vector<float> l (10000), r (10000);
            for (size_t i = 0; i < l.size(); ++i)
            {
                l[i] = rng.nextFloat() * 2.0f - 1.0f;
                r[i] = (rng.nextFloat() * 2.0f - 1.0f) * 0.5f;
            }
            l[5000] = 1.0f;
            r[7000] = -1.0f;

            WaveformPeaks peaks (makeSample (l, r), 64);
            expectEquals (peaks.getNumFrames(), 10000);

            // Column boundaries on bucket boundaries: exact.
            auto cols = peaks.getColumns (0.0, 10240.0, 10);
            expectEquals ((int) cols.size(), 10);
            for (int c = 0; c < 10; ++c)
            {
                float mn = 0.0f, mx = 0.0f;
                bool first = true;
                for (int i = c * 1024; i < juce::jmin (10000, (c + 1) * 1024); ++i)
                    for (auto v : { l[(size_t) i], r[(size_t) i] })
                    {
                        if (first || v < mn) mn = v;
                        if (first || v > mx) mx = v;
                        first = false;
                    }
                expectWithinAbsoluteError (cols[(size_t) c].min, mn, 1.0e-6f);
                expectWithinAbsoluteError (cols[(size_t) c].max, mx, 1.0e-6f);
            }

            // The single loud frames show up in the right column.
            expectEquals (cols[4].max, 1.0f);
            expectEquals (cols[6].min, -1.0f);
        }

        beginTest ("Waveform peaks: zoomed in reads frames, outside is empty");
        {
            WaveformPeaks peaks (makeSample ({ 0.1f, -0.2f, 0.3f, -0.4f }), 64);
            auto cols = peaks.getColumns (0.0, 4.0, 4);
            expectWithinAbsoluteError (cols[1].min, -0.2f, 1.0e-6f);
            expectWithinAbsoluteError (cols[2].max, 0.3f, 1.0e-6f);

            // Eight columns over four frames: each frame covers two columns.
            cols = peaks.getColumns (0.0, 4.0, 8);
            expectWithinAbsoluteError (cols[6].min, -0.4f, 1.0e-6f);
            expectWithinAbsoluteError (cols[7].min, -0.4f, 1.0e-6f);

            // A view past the end of the sample.
            cols = peaks.getColumns (2.0, 10.0, 4);
            expectWithinAbsoluteError (cols[0].max, 0.3f, 1.0e-6f);
            expectEquals (cols[3].max, 0.0f);
            expectEquals (cols[3].min, 0.0f);

            WaveformPeaks empty;
            expect (empty.isEmpty());
            expectEquals ((int) empty.getColumns (0.0, 100.0, 5).size(), 5);
        }

        beginTest ("Recording clean-up trims to the first transient and the tail (SMP-4)");
        {
            const double sr = 48000.0;
            juce::AudioBuffer<float> raw (2, 48000);
            raw.clear();
            // Quiet noise floor, a hit at 0.25 s decaying over 0.1 s, then silence.
            juce::Random rng (7);
            for (int i = 0; i < raw.getNumSamples(); ++i)
                for (int ch = 0; ch < 2; ++ch)
                    raw.setSample (ch, i, (rng.nextFloat() - 0.5f) * 1.0e-5f);
            const int onset = 12000;
            for (int i = 0; i < 4800; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    raw.setSample (ch, onset + i, 0.8f * std::sin ((float) i * 0.05f) * std::exp (-(float) i / 1200.0f));

            auto result = cleanUpRecording (raw, sr, "take");
            expect (result.sample != nullptr);
            if (result.sample == nullptr)
                return;

            expect (result.trimmedStart > onset - 64 && result.trimmedStart <= onset, "start trimmed to the onset");
            expectEquals (result.sample->name, juce::String ("take"));
            expectEquals (result.sample->buffer.getNumChannels(), 2);
            expect (result.sample->getNumFrames() < 48000 - onset, "silent tail removed");
            expect (result.sample->getNumFrames() > 4000, "decay kept");
            expectEquals (result.trimmedStart + result.sample->getNumFrames() + result.trimmedEnd, 48000);
            expectEquals (result.gainDb, 0.0f);
            expectWithinAbsoluteError (result.sample->sampleRate, sr, 1.0e-9);
            // Faded out at the end.
            expectWithinAbsoluteError (result.sample->buffer.getSample (0, result.sample->getNumFrames() - 1), 0.0f, 1.0e-3f);
        }

        beginTest ("Recording clean-up normalises quiet takes lightly and ignores silence");
        {
            juce::AudioBuffer<float> raw (1, 4800);
            for (int i = 0; i < raw.getNumSamples(); ++i)
                raw.setSample (0, i, 0.01f * std::sin ((float) i * 0.1f)); // about -40 dBFS

            auto result = cleanUpRecording (raw, 48000.0, "quiet");
            expect (result.sample != nullptr);
            if (result.sample != nullptr)
            {
                expectWithinAbsoluteError (result.gainDb, 24.0f, 0.01f); // capped
                expect (peakOf (result.sample->buffer) < 0.2f && peakOf (result.sample->buffer) > 0.1f);
            }

            for (int i = 0; i < raw.getNumSamples(); ++i)
                raw.setSample (0, i, 0.1f * std::sin ((float) i * 0.1f)); // -20 dBFS
            result = cleanUpRecording (raw, 48000.0, "medium");
            if (result.sample != nullptr)
                expectWithinAbsoluteError (juce::Decibels::gainToDecibels (peakOf (result.sample->buffer)), -6.0f, 0.2f);

            // Loud takes are left alone.
            raw.applyGain (8.0f);
            result = cleanUpRecording (raw, 48000.0, "loud");
            expectEquals (result.gainDb, 0.0f);

            raw.clear();
            expect (cleanUpRecording (raw, 48000.0, "silent").sample == nullptr);
            expect (cleanUpRecording (juce::AudioBuffer<float>(), 48000.0, "none").sample == nullptr);
        }

        beginTest ("Search and preset filtering (DEV-3)");
        {
            expect (matchesSearch ("Warm Pad", ""));
            expect (matchesSearch ("Warm Pad", "pad"));
            expect (matchesSearch ("Warm Pad", "  PAD warm "));
            expect (! matchesSearch ("Warm Pad", "cold"));
            expect (! matchesSearch ("Warm Pad", "warm bass"));

            std::vector<TrackPreset> presets (4);
            presets[0].name = "Deep Bass";   presets[0].category = "Bass";  presets[0].type = InstrumentType::analog;
            presets[1].name = "Wobble Bass"; presets[1].category = "Bass";  presets[1].type = InstrumentType::wave;
            presets[2].name = "Bass Kit";    presets[2].category = "Drums"; presets[2].type = InstrumentType::drumKit;
            presets[3].name = "Glass Keys";  presets[3].category = "Keys";  presets[3].type = InstrumentType::analog;

            expect (filterPresets (presets, "Bass", "") == std::vector<int> { 0, 1 });
            expect (filterPresets (presets, "Keys", "") == std::vector<int> { 3 });
            expect (filterPresets (presets, "", "") == std::vector<int> { 0, 1, 2, 3 });
            // Search spans categories.
            expect (filterPresets (presets, "Keys", "bass") == std::vector<int> { 0, 1, 2 });
            // Type filter.
            expect (filterPresets (presets, "Bass", "", (int) InstrumentType::wave) == std::vector<int> { 1 });
            expect (filterPresets (presets, "", "ass", (int) InstrumentType::analog) == std::vector<int> { 0, 3 });
        }

        beginTest ("Sample references and recording files");
        {
            expectEquals (sampleDisplayName ("set:rec-20261029-101500.wav"), juce::String ("rec-20261029-101500"));
            expectEquals (sampleDisplayName ("user:My Loop.aiff"), juce::String ("My Loop"));
            expectEquals (sampleDisplayName (""), juce::String());
            expect (sampleDisplayName ("factory:unknown-id").isNotEmpty());

            expect (isSetRecording ("set:rec-1.wav"));
            expect (! isSetRecording ("set:import.wav"));
            expect (! isSetRecording ("user:rec-1.wav"));

            expectEquals (startFrameFor (0.0f, 1000), 0);
            expectEquals (startFrameFor (0.5f, 1000), 500);
            expectEquals (startFrameFor (1.0f, 1000), 999);
            expectEquals (startFrameFor (-1.0f, 1000), 0);
            expectEquals (startFrameFor (0.5f, 0), 0);

            auto folder = juce::File::createTempFile ("rb-set");
            folder.createDirectory();
            const juce::Time time (2026, 9, 29, 14, 30, 5, 0, true);

            auto first = makeRecordingFile (folder.getChildFile ("samples"), time);
            expectEquals (first.getFileName(), juce::String ("rec-20261029-143005.wav"));

            expect (listSetSamples (folder).isEmpty());

            auto sample = makeSample ({ 0.5f, -0.5f, 0.25f, 0.0f });
            expect (SampleLibrary::writeWav (*sample, first));
            auto second = makeRecordingFile (folder.getChildFile ("samples"), time);
            expectEquals (second.getFileName(), juce::String ("rec-20261029-143005-2.wav"));
            expect (SampleLibrary::writeWav (*sample, second));
            folder.getChildFile ("samples").getChildFile ("notes.txt").replaceWithText ("x");

            auto refs = listSetSamples (folder);
            expectEquals (refs.size(), 2);
            expect (refs.contains ("set:rec-20261029-143005.wav"));
            expect (refs.contains ("set:rec-20261029-143005-2.wav"));

            // A written recording loads back through the library as a set: reference.
            SampleLibrary library;
            auto loaded = library.get ("set:" + first.getFileName(), folder);
            expect (loaded != nullptr);
            if (loaded != nullptr)
                expectEquals (loaded->getNumFrames(), 4);

            folder.deleteRecursively();
        }
    }
};

static BrowserSampleTests browserSampleTests;

// Factory sound warm-up runs on a background thread while the message thread also asks for
// samples: both must end up sharing one buffer per sound, with nothing rendered twice into
// the cache.
class SampleLibraryThreadingTests : public juce::UnitTest
{
public:
    SampleLibraryThreadingTests() : juce::UnitTest ("Sample library threading", "samples") {}

    void runTest() override
    {
        beginTest ("Warm-up fills the cache; concurrent requests share one buffer");
        rb::SampleLibrary library;
        const auto& sounds = rb::FactorySounds::list();

        std::thread background ([&] { library.warmUpFactorySounds (nullptr); });

        // Meanwhile, request the same sounds in reverse order from this thread.
        std::vector<rb::SamplePtr> mine;
        for (auto it = sounds.rbegin(); it != sounds.rend(); ++it)
            mine.push_back (library.get ("factory:" + it->id, {}));

        background.join();

        for (auto& info : sounds)
            expect (library.isCached ("factory:" + info.id), info.id);

        size_t i = 0;
        for (auto it = sounds.rbegin(); it != sounds.rend(); ++it, ++i)
            expect (mine[i] == library.get ("factory:" + it->id, {}), "shared buffer for " + it->id);

        beginTest ("Warm-up stops when asked");
        rb::SampleLibrary fresh;
        int calls = 0;
        fresh.warmUpFactorySounds ([&] { return ++calls > 3; });
        int cached = 0;
        for (auto& info : sounds)
            cached += fresh.isCached ("factory:" + info.id) ? 1 : 0;
        expectEquals (cached, 3);
    }
};

static SampleLibraryThreadingTests sampleLibraryThreadingTests;
