// Engine-level render tests: transport, rack, and mixer together, driven through a Session.

#include "AllocationCounter.h"
#include "Analysis.h"
#include "TestHelpers.h"
#include "TestMachines.h"
#include "core/DemoSong.h"
#include "core/machines/TestToneMachine.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace {
    float maxDifference(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b) {
        float difference = 0.0f;
        for (int ch = 0; ch < a.getNumChannels(); ch++) {
            for (int i = 0; i < a.getNumSamples(); i++) {
                difference = std::max(difference, std::abs(a.getSample(ch, i) - b.getSample(ch, i)));
            }
        }
        return difference;
    }
}

TEST_CASE("Engine is silent while stopped", "[engine]") {
    Rig rig;
    REQUIRE(rig.session.loadSong(makeDemoSong()).wasOk());

    CHECK(analyse(rig.render(rig.renderer.seconds(2.0))).peak <= 0.0f); // exactly silent
}

TEST_CASE("Demo song renders finite, audible audio", "[engine]") {
    Rig rig;
    REQUIRE(rig.session.loadSong(makeDemoSong()).wasOk());

    rig.session.setPlaying(true);
    const auto stats = analyse(rig.render(rig.renderer.seconds(5.0)));

    CHECK(stats.allFinite);
    CHECK(stats.rms > 1.0e-3);
    CHECK(stats.peak < 1.0f);
}

TEST_CASE("Audio does not depend on the host block size", "[engine]") {
    const int hostBlock = GENERATE(17, 256, 2048); // 2048 exceeds the prepared size: chunked
    CAPTURE(hostBlock);

    Rig reference;
    REQUIRE(reference.session.loadSong(makeDemoSong()).wasOk());
    reference.session.setPlaying(true);
    const auto expected = reference.render(96000, 512);

    Rig rig;
    REQUIRE(rig.session.loadSong(makeDemoSong()).wasOk());
    rig.session.setPlaying(true);
    const auto actual = rig.render(96000, hostBlock);

    CHECK(maxDifference(expected, actual) < 1.0e-5f);
}

TEST_CASE("Stopping releases notes and the tail decays to silence", "[engine][transport]") {
    Rig rig;
    REQUIRE(rig.session.loadSong(makeDemoSong()).wasOk());

    rig.session.setPlaying(true);
    rig.render(rig.renderer.seconds(2.0));
    rig.session.setPlaying(false);
    const auto tail = rig.render(rig.renderer.seconds(10.0)); // delay and reverb returns ring on

    const int lastHalf = static_cast<int>(rig.renderer.seconds(0.5));
    CHECK(analyse(tail, tail.getNumSamples() - lastHalf, lastHalf).peak < 1.0e-4f);
    CHECK(analyse(tail).subnormals == 0);
}

TEST_CASE("Restarting plays from the top", "[engine][transport]") {
    Rig rig;
    REQUIRE(rig.session.loadSong(makeDemoSong()).wasOk());

    rig.session.setPlaying(true);
    const auto first = rig.render(24000);
    rig.session.setPlaying(false);
    rig.render(rig.renderer.seconds(12.0)); // let every tail die out
    rig.session.setPlaying(true);
    const auto second = rig.render(24000);

    // Not bit-exact: filters that run continuously (like the acid ladder) carry a little state
    // across the restart. A restart that doesn't rewind differs by ~0.4.
    CHECK(maxDifference(first, second) < 0.02f);
    CHECK_THAT(rig.engine.getPlayheadBeat(),
               Catch::Matchers::WithinAbs(24000.0 / (48000.0 * 60.0 / rig.engine.getTempo()), 1.0e-9));
}

TEST_CASE("Rack holds at most 14 machines", "[engine][rack]") {
    Rig rig;
    for (int i = 0; i < Engine::maxMachines; i++) {
        CHECK(rig.session.addMachine(TestToneMachine::typeName) == i);
    }
    CHECK(rig.session.addMachine(TestToneMachine::typeName) == -1);
    CHECK(rig.session.addMachine(DrumMachine::typeName) == -1);

    rig.render(512);
    CHECK(rig.engine.getNumMachines() == Engine::maxMachines);
}

TEST_CASE("Machine and master volume scale the output", "[engine][mixer]") {
    Rig rig;
    const int m = rig.session.addMachine(TestToneMachine::typeName);
    Pattern pattern;
    pattern.notes.push_back({ 0.0, 4.0, 69, 1.0f });
    rig.session.setPattern(m, 0, pattern);

    rig.session.setPlaying(true);
    const auto full = Analysis::rms(rig.render(24000), 0, 2048, 20000); // after any gain ramp

    rig.session.setPlaying(false);
    rig.render(rig.renderer.seconds(1.0));
    rig.session.setMachineVolume(m, 0.4f * 0.8f);
    rig.session.setMasterVolume(0.4f * 0.8f);
    rig.session.setPlaying(true);
    const auto quieter = Analysis::rms(rig.render(24000), 0, 2048, 20000);

    CHECK_THAT(quieter / full, Catch::Matchers::WithinRel(0.16, 0.01));
}

TEST_CASE("Muted machine volume produces silence", "[engine][mixer]") {
    Rig rig;
    REQUIRE(rig.session.loadSong(makeDemoSong()).wasOk());
    for (int i = 0; i < rig.session.getNumMachines(); i++) {
        rig.session.setMachineVolume(i, 0.0f);
    }

    rig.session.setPlaying(true);
    CHECK(analyse(rig.render(rig.renderer.seconds(1.0))).peak <= 0.0f);
}

TEST_CASE("The audio callback never touches the heap, even while edits arrive", "[engine][realtime]") {
    RecordingMachine::registerType();
    Rig rig;
    REQUIRE(rig.session.loadSong(makeDemoSong()).wasOk());
    rig.session.addMachine(RecordingMachine::typeName);
    rig.session.setMixerParameter(1, "delaySend", 0.6f);
    rig.session.setMixerParameter(2, "reverbSend", 0.8f);
    rig.session.setPlaying(true);

    juce::AudioBuffer<float> block(2, 512);
    rig.engine.process(block); // first block applies the load

    std::size_t touches = 0;
    for (int i = 0; i < 400; i++) {
        // A steady stream of edits of every kind, as a busy UI would produce.
        Pattern pattern;
        pattern.bars = (i % 2 == 0) ? 1 : 2;
        pattern.notes.push_back({ (i % 16) * 0.25, 0.5, 40 + i % 40, 0.9f });
        rig.session.setPattern(i % 3, i % 4, pattern);
        if (i % 50 == 10) {
            rig.session.selectPattern(i % 3, (i / 50) % 4);
        }
        if (i == 200) {
            rig.session.removeMachine(2);
        }
        if (i == 250) {
            rig.session.addMachine(TestToneMachine::typeName);
        }
        rig.session.setParameter(1, "cutoff", 200.0f + static_cast<float>(i));
        rig.session.setParameter(2, "lfoToCutoff", 0.002f * static_cast<float>(i));
        rig.session.setParameter(0, "s1.tune", static_cast<float>(i % 12));
        rig.session.setMixerParameter(i % 3, "eqLow", static_cast<float>(i % 30) - 15.0f);
        rig.session.setMixerParameter(i % 3, "pan", static_cast<float>(i % 3) - 1.0f);
        rig.session.setMixerParameter(i % 3, "width", static_cast<float>(i % 3) * 0.7f);
        rig.session.setMixerParameter(0, "solo", (i / 60) % 2 == 0 ? 0.0f : 1.0f);
        rig.session.setMasterParameter("delayBeats", 0.25f * static_cast<float>(1 + i % 6));
        rig.session.setTempo(100.0 + i % 40);

        AllocationCounter::start();
        rig.engine.process(block);
        touches += AllocationCounter::stop();

        rig.session.collectGarbage();
    }

    CHECK(touches == 0);
}
