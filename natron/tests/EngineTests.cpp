// Engine-level render tests: transport, rack, and mixer together, rendered offline.

#include "TestHelpers.h"
#include "core/DemoSong.h"
#include "core/machines/TestToneMachine.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

TEST_CASE("Engine is silent while stopped", "[engine]") {
    Engine engine;
    loadDemoSong(engine);
    EngineRenderer r(engine);

    const auto audio = r.render(r.seconds(2.0));

    CHECK(analyse(audio).peak <= 0.0f); // exactly silent
}

TEST_CASE("Demo song renders finite, audible audio", "[engine]") {
    Engine engine;
    loadDemoSong(engine);
    EngineRenderer r(engine);

    engine.setPlaying(true);
    const auto stats = analyse(r.render(r.seconds(5.0)));

    CHECK(stats.allFinite);
    CHECK(stats.rms > 1.0e-3);
    CHECK(stats.peak < 1.0f);
}

TEST_CASE("Audio does not depend on the host block size", "[engine]") {
    const int hostBlock = GENERATE(17, 256, 2048); // 2048 exceeds the prepared size: chunked
    CAPTURE(hostBlock);

    Engine reference;
    loadDemoSong(reference);
    EngineRenderer referenceRenderer(reference, 48000.0, 512);
    reference.setPlaying(true);
    const auto expected = referenceRenderer.render(96000, 512);

    Engine engine;
    loadDemoSong(engine);
    EngineRenderer r(engine, 48000.0, 512);
    engine.setPlaying(true);
    const auto actual = r.render(96000, hostBlock);

    float maxDifference = 0.0f;
    for (int ch = 0; ch < 2; ch++) {
        for (int i = 0; i < expected.getNumSamples(); i++) {
            maxDifference = std::max(maxDifference, std::abs(expected.getSample(ch, i) - actual.getSample(ch, i)));
        }
    }
    CHECK(maxDifference < 1.0e-5f);
}

TEST_CASE("Stopping releases notes and the tail decays to silence", "[engine][transport]") {
    Engine engine;
    loadDemoSong(engine);
    EngineRenderer r(engine);

    engine.setPlaying(true);
    r.render(r.seconds(1.0));
    engine.setPlaying(false);
    const auto tail = r.render(r.seconds(1.0));

    const int lastQuarter = static_cast<int>(r.seconds(0.25));
    const auto end = analyse(tail, tail.getNumSamples() - lastQuarter, lastQuarter);
    CHECK(end.peak <= 0.0f);
    CHECK(analyse(tail).subnormals == 0);
}

TEST_CASE("Restarting plays from the top", "[engine][transport]") {
    Engine engine;
    loadDemoSong(engine);
    EngineRenderer r(engine);

    engine.setPlaying(true);
    const auto first = r.render(24000);
    engine.setPlaying(false);
    r.render(r.seconds(1.0));
    engine.setPlaying(true);
    const auto second = r.render(24000);

    float maxDifference = 0.0f;
    for (int i = 0; i < first.getNumSamples(); i++) {
        maxDifference = std::max(maxDifference, std::abs(first.getSample(0, i) - second.getSample(0, i)));
    }
    CHECK(maxDifference < 1.0e-5f);
    CHECK_THAT(engine.getPlayheadBeat(), Catch::Matchers::WithinAbs(24000.0 / (48000.0 * 60.0 / engine.getTempo()), 1.0e-9));
}

TEST_CASE("Rack holds at most 14 machines", "[engine][rack]") {
    Engine engine;
    for (int i = 0; i < Engine::maxMachines; i++) {
        CHECK(engine.addMachine(std::make_unique<TestToneMachine>()) == i);
    }
    CHECK(engine.addMachine(std::make_unique<TestToneMachine>()) == -1);
    CHECK(engine.getNumMachines() == Engine::maxMachines);
}

TEST_CASE("Machine and master volume scale the output", "[engine][mixer]") {
    Engine engine;
    const int m = engine.addMachine(std::make_unique<TestToneMachine>());
    engine.getPattern(m).notes.push_back({ 0.0, 4.0, 69, 1.0f });
    EngineRenderer r(engine);

    engine.setPlaying(true);
    const auto full = analyse(r.render(24000));

    engine.setPlaying(false);
    r.render(r.seconds(1.0));
    engine.setMachineVolume(m, 0.4f);
    engine.setMasterVolume(0.4f);
    engine.setPlaying(true);
    const auto quieter = analyse(r.render(24000));

    CHECK_THAT(quieter.rms / full.rms, Catch::Matchers::WithinRel(0.25, 0.01));
}

TEST_CASE("Muted machine volume produces silence", "[engine][mixer]") {
    Engine engine;
    loadDemoSong(engine);
    for (int i = 0; i < engine.getNumMachines(); i++) {
        engine.setMachineVolume(i, 0.0f);
    }
    EngineRenderer r(engine);

    engine.setPlaying(true);
    CHECK(analyse(r.render(r.seconds(1.0))).peak <= 0.0f);
}
