#include "TestHelpers.h"

using namespace rb;
using namespace rbtest;

class AutomationTests : public juce::UnitTest
{
public:
    AutomationTests() : juce::UnitTest ("Automation and recording", "engine") {}

    void runTest() override
    {
        beginTest ("Clip automation drives instrument parameters during playback (DEV-31)");
        {
            ProbeScope probes;
            Rig rig;
            Set set;
            set.tempo = 120.0;
            addTrack (set, simplePreset());

            auto clip = std::make_shared<Clip> (*clipWithNotes ({ note (0, 60) }));
            AutomationLane lane;
            lane.target = { ParamTarget::instrument, 0, 0 };
            lane.points = { { 0.0, -40.0f }, { 4.0, 0.0f } };
            clip->automation.push_back (lane);
            set.tracks[0].slots[0] = clip;
            rig.open (set);

            rig.post (Command::Type::launchClip, set.tracks[0].id, 0);
            rig.renderBeats (2.0);

            auto& history = probes.logs.back()->parameterHistory;
            expect (! history.empty());
            // Halfway through the bar the value is about halfway along the ramp.
            expectWithinAbsoluteError (history.back()[0], -20.0f, 1.0f);

            // Stopped: the stored value applies again.
            rig.post (Command::Type::stop);
            rig.render (Rig::blockSize * 2);
            expectWithinAbsoluteError (history.back()[0], getInstrumentSpec (InstrumentType::analog).params[0].defaultValue, 0.001f);
        }

        beginTest ("Resample records the engine output (SMP-5)");
        {
            ProbeScope probes;
            Rig rig;
            Set set;
            set.metronome = true;
            addTrack (set, simplePreset());
            set.tracks[0].slots[0] = clipWithNotes ({});
            rig.open (set);

            rig.session.startRecording (SessionProcessor::RecordSource::output, 2.0);
            expect (rig.session.isRecording());
            rig.post (Command::Type::launchClip, set.tracks[0].id, 0);
            rig.render (12000);
            auto recorded = rig.session.stopRecording();

            expectEquals (recorded.getNumSamples(), 12000);
            expect (recorded.getMagnitude (0, 0, recorded.getNumSamples()) > 0.05f); // the metronome click
            expect (! rig.session.isRecording());
        }

        beginTest ("Recording stops by itself when the buffer is full");
        {
            Rig rig;
            rig.open (Set());
            rig.session.startRecording (SessionProcessor::RecordSource::input, 0.1);
            rig.render (48000);
            expect (! rig.session.isRecording());
            expectEquals (rig.session.stopRecording().getNumSamples(), 4800);
        }

        beginTest ("Transient trim finds the onset");
        {
            juce::AudioBuffer<float> buffer (1, 1000);
            buffer.clear();
            for (int i = 600; i < 700; ++i)
                buffer.setSample (0, i, 0.5f);
            const int start = findFirstTransient (buffer);
            expect (start <= 600 && start >= 560);
        }
    }
};

static AutomationTests automationTests;
