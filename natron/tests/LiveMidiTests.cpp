// Live MIDI input: devices and the on-screen keyboard play the target machine.

#include "TestHelpers.h"
#include "TestMachines.h"

#include <catch2/catch_test_macros.hpp>
#include <thread>

namespace {
    struct LiveRig : Rig {
        LiveRig() {
            RecordingMachine::registerType();
            session.addMachine(RecordingMachine::typeName);
            session.addMachine(RecordingMachine::typeName);
            render(1);
            first = RecordingMachine::instances()[RecordingMachine::instances().size() - 2];
            second = RecordingMachine::instances().back();
            for (auto* r : { first, second }) {
                r->events.clear();
                r->position = 0;
            }
        }
        RecordingMachine* first = nullptr;
        RecordingMachine* second = nullptr;
    };
}

TEST_CASE("Live notes play on the target machine at the next block", "[live]") {
    LiveRig rig;
    rig.engine.setLiveTarget(1);
    REQUIRE(rig.engine.pushLiveMidi(juce::MidiMessage::noteOn(1, 64, 0.8f)));
    rig.render(512);
    REQUIRE(rig.engine.pushLiveMidi(juce::MidiMessage::noteOff(1, 64)));
    rig.render(512);

    CHECK(rig.first->events.empty());
    REQUIRE(rig.second->events.size() == 2);
    CHECK(rig.second->events[0] == on(0, 64));
    CHECK(rig.second->events[1] == off(512, 64));
}

TEST_CASE("Live notes play while stopped and while playing", "[live]") {
    LiveRig rig;
    rig.engine.setLiveTarget(0);
    rig.engine.pushLiveMidi(juce::MidiMessage::noteOn(1, 60, 0.8f));
    rig.render(512);
    rig.session.setPlaying(true);
    rig.engine.pushLiveMidi(juce::MidiMessage::noteOn(1, 62, 0.8f));
    rig.render(512);
    CHECK(rig.first->noteOns().size() == 2);
}

TEST_CASE("Only playable messages are queued", "[live]") {
    LiveRig rig;
    CHECK_FALSE(rig.engine.pushLiveMidi(juce::MidiMessage::createSysExMessage("hi", 2)));
    CHECK_FALSE(rig.engine.pushLiveMidi(juce::MidiMessage::programChange(1, 3)));
    CHECK(rig.engine.pushLiveMidi(juce::MidiMessage::controllerEvent(1, 1, 64)));
    rig.engine.setLiveTarget(99); // no such machine: dropped quietly
    rig.engine.pushLiveMidi(juce::MidiMessage::noteOn(1, 60, 0.8f));
    rig.render(512);
    CHECK(rig.first->events.empty());
    CHECK(rig.second->events.empty());
}

TEST_CASE("Several input threads at once lose nothing", "[live]") {
    LiveRig rig;
    rig.engine.setLiveTarget(0);
    std::atomic<int> accepted { 0 };
    auto producer = [&](int base) {
        for (int i = 0; i < 400; i++) {
            if (rig.engine.pushLiveMidi(juce::MidiMessage::noteOn(1, base + i % 20, 0.5f))) {
                accepted++;
            }
        }
    };
    std::thread a(producer, 30);
    std::thread b(producer, 70);
    a.join();
    b.join();
    rig.render(512);
    CHECK(accepted.load() == 800);
    CHECK(rig.first->noteOns().size() == 800);
}
