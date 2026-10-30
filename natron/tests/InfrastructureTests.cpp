// Phase 4 infrastructure: swing, swappable resources, and machine data.

#include "TestHelpers.h"
#include "TestMachines.h"
#include "core/SongFile.h"
#include "core/SwappableResource.h"

#include <catch2/catch_test_macros.hpp>
#include <thread>

// ---------------------------------------------------------------------------- Swing

TEST_CASE("Swing delays only off-beat 1/16 notes", "[swing]") {
    CHECK(juce::exactlyEqual(PatternPlayer::swingOffset(0.0, 0.75f), 0.0));
    CHECK(juce::exactlyEqual(PatternPlayer::swingOffset(0.5, 0.75f), 0.0));
    CHECK(juce::exactlyEqual(PatternPlayer::swingOffset(0.25, 0.5f), 0.0));
    // The off-beat 1/16 lands at `swing` of its 1/8: 75% = 0.375 beats into the pair...
    CHECK(juce::exactlyEqual(PatternPlayer::swingOffset(0.25, 0.75f), 0.125));
    // ...66% = the triplet position, 1/3 beat into the pair.
    CHECK(std::abs(PatternPlayer::swingOffset(1.75, 2.0f / 3.0f) - 1.0 / 12.0) < 1.0e-6);
    CHECK(juce::exactlyEqual(PatternPlayer::swingOffset(0.3, 0.75f), 0.0)); // off the grid: untouched
}

TEST_CASE("Swing moves notes in the engine", "[swing]") {
    RecordingMachine::registerType();
    Rig rig;
    const int m = rig.session.addMachine(RecordingMachine::typeName);
    Pattern p;
    p.notes = { { 0.0, 0.1, 60, 0.8f }, { 0.25, 0.1, 62, 0.8f }, { 0.5, 0.1, 64, 0.8f } };
    rig.session.setPattern(m, 0, p);
    rig.session.setSwing(0.625f); // exactly representable, so sample positions are exact
    rig.render(1);
    auto* recorder = RecordingMachine::instances().back();
    recorder->events.clear();
    recorder->position = 0;

    rig.session.setPlaying(true);
    rig.render(24000);

    const auto ons = recorder->noteOns();
    REQUIRE(ons.size() == 3);
    CHECK(ons[0].sample == 0);
    CHECK(ons[1].sample == 7500); // 62.5% of an 8th (12000 samples)
    CHECK(ons[2].sample == 12000);
}

TEST_CASE("Swing is clamped and saved", "[swing][songfile]") {
    Rig rig;
    rig.session.setSwing(0.9f);
    CHECK(juce::exactlyEqual(rig.session.getSwing(), Song::maxSwing));
    rig.session.setSwing(0.6f);

    juce::MemoryOutputStream out;
    REQUIRE(rig.session.saveToStream(out).wasOk());
    Rig other;
    juce::MemoryInputStream in(out.getData(), out.getDataSize(), false);
    REQUIRE(other.session.loadFromStream(in).wasOk());
    CHECK(juce::exactlyEqual(other.session.getSwing(), 0.6f));
    CHECK(juce::exactlyEqual(other.engine.getSwing(), 0.6f));
}

// ---------------------------------------------------------------------------- SwappableResource

TEST_CASE("A swapped-out object is freed only once the audio side has moved on", "[swappable]") {
    struct Counted {
        explicit Counted(int& counter) : alive(counter) { alive++; }
        ~Counted() { alive--; }
        int& alive;
    };
    int alive = 0;
    {
        SwappableResource<Counted> resource;
        resource.set(std::make_unique<Counted>(alive));
        const auto* first = resource.acquire(); // the "audio thread" is using the first object

        resource.set(std::make_unique<Counted>(alive));
        CHECK(alive == 2);                      // first is retired but still in use
        CHECK(resource.numRetired() == 1);
        CHECK(resource.get() != first);

        resource.acquire();                     // the audio thread picks up the new one
        resource.collect();
        CHECK(alive == 1);
        CHECK(resource.numRetired() == 0);
    }
    CHECK(alive == 0);
}

TEST_CASE("Swapping under a concurrent reader never frees what it reads", "[swappable]") {
    struct Payload {
        int a = 0;
        int b = 0; // always equal to a while alive
        ~Payload() { a = -1; b = -2; }
    };
    SwappableResource<Payload> resource;
    resource.set(std::make_unique<Payload>());

    std::atomic<bool> stop { false };
    std::atomic<int> mismatches { 0 };
    std::thread reader([&] {
        while (!stop.load()) {
            const auto* p = resource.acquire();
            for (int i = 0; i < 50; i++) {
                if (p->a != p->b) {
                    mismatches++;
                }
            }
        }
    });
    for (int i = 1; i < 20000; i++) {
        auto next = std::make_unique<Payload>();
        next->a = next->b = i;
        resource.set(std::move(next));
    }
    stop.store(true);
    reader.join();
    CHECK(mismatches.load() == 0);
    CHECK(resource.numRetired() <= 1);
}

// ---------------------------------------------------------------------------- Machine data

TEST_CASE("Machine data is applied live, saved, and validated", "[machinedata]") {
    DataMachine::registerType();
    Rig rig;
    const int m = rig.session.addMachine(DataMachine::typeName);
    rig.session.setMixerParameter(m, "volume", 1.0f);
    rig.session.setMasterParameter("volume", 1.0f);
    rig.render(512);
    CHECK(juce::exactlyEqual(rig.session.getMachineData(m).getProperty("level"), juce::var(0.25f)));

    juce::ValueTree data("TestData");
    data.setProperty("level", 0.5f, nullptr);
    REQUIRE(rig.session.setMachineData(m, data).wasOk());
    const auto audio = rig.render(512);
    CHECK(std::abs(audio.getSample(0, 511) - 0.5f) < 1.0e-4f);
    rig.session.collectGarbage();

    juce::ValueTree bad("TestData");
    bad.setProperty("level", -1.0f, nullptr);
    CHECK(rig.session.setMachineData(m, bad).failed());
    CHECK(juce::exactlyEqual(static_cast<float>(rig.session.getMachineData(m).getProperty("level")), 0.5f));

    juce::MemoryOutputStream out;
    REQUIRE(rig.session.saveToStream(out).wasOk());
    CHECK(out.toString().contains("<Data>"));
    Rig other;
    juce::MemoryInputStream in(out.getData(), out.getDataSize(), false);
    REQUIRE(other.session.loadFromStream(in).wasOk());
    CHECK(juce::exactlyEqual(static_cast<float>(other.session.getMachineData(0).getProperty("level")), 0.5f));
}

TEST_CASE("A song whose machine data is invalid doesn't load", "[machinedata]") {
    DataMachine::registerType();
    Song song;
    MachineState state;
    state.type = DataMachine::typeName;
    state.name = "Broken";
    state.data = juce::ValueTree("TestData");
    state.data.setProperty("level", -2.0f, nullptr);
    song.machines.push_back(state);

    Rig rig;
    const auto result = rig.session.loadSong(song);
    CHECK(result.failed());
    CHECK(result.getErrorMessage().contains("Broken"));
}
