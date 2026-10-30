// CPU benchmarks: how much of one core each machine (and a full rack) needs at 48 kHz.
// Hidden from normal runs; build Release and run:  NatronTests "[.benchmark]"
// A phone core is several times slower than a desktop one: keep the full rack well under ~25% here.

#include "TestHelpers.h"
#include "core/MachineRegistry.h"
#include "core/effects/Effect.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>

namespace {
    // Renders `seconds` and returns the share of one core used (0.1 = 10%).
    double coreShare(Rig& rig, double seconds) {
        juce::AudioBuffer<float> block(2, 256);
        const auto blocks = static_cast<int>(seconds * 48000.0 / 256.0);
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < blocks; i++) {
            rig.engine.process(block);
        }
        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
        return elapsed.count() / seconds;
    }

    Pattern busyPattern(int machineIndex, const juce::String& type) {
        Pattern pattern;
        if (type == "Drums") {
            for (int step = 0; step < 16; step++) {
                for (int slot = 0; slot < 8; slot += (step % 2 == 0 ? 1 : 3)) {
                    pattern.notes.push_back({ step * 0.25, 0.25, 36 + slot, 0.9f });
                }
            }
            return pattern;
        }
        // Eight-note chords, re-struck every beat: every voice busy.
        for (int beat = 0; beat < 4; beat++) {
            for (int n = 0; n < 8; n++) {
                pattern.notes.push_back({ static_cast<double>(beat), 1.0, 40 + machineIndex + n * 5, 0.8f });
            }
        }
        return pattern;
    }
}

TEST_CASE("Benchmark: each machine at full polyphony", "[.benchmark]") {
    for (const auto& type : MachineRegistry::getTypeNames()) {
        if (type == "TestTone") {
            continue;
        }
        Rig rig;
        const int m = rig.session.addMachine(type);
        rig.session.setPattern(m, 0, busyPattern(0, type));
        rig.session.setPlaying(true);
        rig.render(4800); // settle
        const double share = coreShare(rig, 10.0);
        std::printf("%-12s %6.2f%% of a core\n", type.toRawUTF8(), share * 100.0);
    }
    SUCCEED();
}

TEST_CASE("Benchmark: a full rack with effects and sends", "[.benchmark]") {
    Rig rig;
    const auto types = MachineRegistry::getTypeNames();
    const auto effects = EffectRegistry::getTypeNames();
    int m = 0;
    for (int i = 0; m < Engine::maxMachines; i++) {
        const auto& type = types[i % types.size()];
        if (type == "TestTone") {
            continue;
        }
        rig.session.addMachine(type);
        rig.session.setPattern(m, 0, busyPattern(m, type));
        rig.session.setEffect(m, 0, effects[(2 * m) % effects.size()]);
        rig.session.setEffect(m, 1, effects[(2 * m + 1) % effects.size()]);
        rig.session.setMixerParameter(m, "reverbSend", 0.3f);
        rig.session.setMixerParameter(m, "delaySend", 0.2f);
        rig.session.setMixerParameter(m, "eqMid", 3.0f);
        m++;
    }
    rig.session.setMasterParameter("eq1Gain", 2.0f);
    rig.session.setPlaying(true);
    rig.render(4800);
    const double share = coreShare(rig, 10.0);
    std::printf("Full rack (14 machines, 28 effects): %.2f%% of a core\n", share * 100.0);
    SUCCEED();
}
