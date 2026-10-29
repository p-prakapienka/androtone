#include "TestHelpers.h"

using namespace rb;

// Renders every demo set through the real devices: a end-to-end check of instruments,
// effects, presets and gain staging.
class DemoRenderTests : public juce::UnitTest
{
public:
    DemoRenderTests() : juce::UnitTest ("Demo renders", "render") {}

    void runTest() override
    {
        auto outFolder = juce::SystemStats::getEnvironmentVariable ("RB_RENDER_DIR", {});

        for (auto& demo : makeDemoSets())
        {
            beginTest ("Demo renders cleanly: " + demo.name);

            SampleLibrary library;
            auto audio = OfflineRenderer::renderScenes (demo, {}, library, 48000.0, 2.0);
            expect (audio.getNumSamples() > 48000);

            float peak = 0.0f;
            bool finite = true;
            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            {
                peak = juce::jmax (peak, audio.getMagnitude (ch, 0, audio.getNumSamples()));
                for (int i = 0; i < audio.getNumSamples(); ++i)
                    finite = finite && std::isfinite (audio.getSample (ch, i));
            }

            expect (finite);
            expect (peak > 0.1f, "too quiet: " + juce::String (peak));
            expect (peak < 1.5f, "too loud: " + juce::String (peak));
            logMessage (demo.name + " peak " + juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS");

            if (outFolder.isNotEmpty())
                OfflineRenderer::write (audio, 48000.0, OfflineRenderer::Format::wav16,
                                        juce::File (outFolder).getChildFile (juce::File::createLegalFileName (demo.name) + ".wav"));
        }
    }
};

static DemoRenderTests demoRenderTests;
