// Fingerprint ("golden") renders: one per machine plus the full demo song. See Fingerprint.h.

#include "Fingerprint.h"
#include "TestHelpers.h"
#include "core/DemoSong.h"
#include "core/effects/Effect.h"
#include "core/machines/AcidBass.h"
#include "core/machines/DrumMachine.h"
#include "core/machines/SubtractiveSynth.h"

#include <catch2/catch_test_macros.hpp>

namespace {
    void checkGolden(const juce::String& name, const juce::AudioBuffer<float>& audio) {
        REQUIRE(analyse(audio).allFinite);
        const auto problem = Fingerprint::compare(name, Fingerprint::compute(audio, 48000.0));
        INFO(name << ": " << problem);
        CHECK(problem.isEmpty());
    }
}

TEST_CASE("Golden: Subtractive", "[golden]") {
    MachineRig rig(SubtractiveSynth::typeName);
    rig.set("filterEnvAmount", 0.6f);
    rig.set("resonance", 0.5f);
    checkGolden("subtractive", rig.play({ { 0.0, 1.5, 48, 0.9f }, { 0.0, 1.5, 55, 0.7f }, { 2.0, 1.0, 60, 0.8f } }, 2.0));
}

TEST_CASE("Golden: Acid", "[golden]") {
    MachineRig rig(AcidBass::typeName);
    std::vector<Note> line;
    const int pitches[] = { 36, 36, 48, 36, 39, 41, 36, 46 };
    for (int i = 0; i < 8; i++) {
        line.push_back({ i * 0.5, i == 4 ? 0.6 : 0.3, pitches[i], i % 3 == 0 ? 1.0f : 0.7f });
    }
    checkGolden("acid", rig.play(line, 2.0));
}

TEST_CASE("Golden: Drums", "[golden]") {
    MachineRig rig(DrumMachine::typeName);
    std::vector<Note> hits;
    for (int slot = 0; slot < DrumMachine::numSlots; slot++) {
        hits.push_back({ slot * 0.5, 0.25, DrumMachine::firstNote + slot, 1.0f });
    }
    checkGolden("drums", rig.play(hits, 2.0));
}

TEST_CASE("Golden: demo song through the mixer", "[golden]") {
    Rig rig;
    REQUIRE(rig.session.loadSong(makeDemoSong()).wasOk());
    rig.session.setPlaying(true);
    checkGolden("demo-song", rig.render(rig.renderer.seconds(4.0)));
}

TEST_CASE("Golden: every effect on a fixed signal", "[golden]") {
    // A detuned saw pair through each effect in turn (0.4 s each), back to back.
    const auto types = EffectRegistry::getTypeNames();
    const int each = 19200;
    juce::AudioBuffer<float> all(2, each * types.size());
    for (int t = 0; t < types.size(); t++) {
        auto effect = EffectRegistry::create(types[t]);
        effect->prepare(48000.0, 512);
        juce::AudioBuffer<float> block(2, 512);
        double phaseA = 0.0, phaseB = 0.0;
        for (int start = 0; start < each; start += 512) {
            const int n = juce::jmin(512, each - start);
            for (int i = 0; i < n; i++) {
                phaseA = std::fmod(phaseA + 110.0 / 48000.0, 1.0);
                phaseB = std::fmod(phaseB + 110.7 / 48000.0, 1.0);
                block.setSample(0, i, static_cast<float>(0.4 * (2.0 * phaseA - 1.0)));
                block.setSample(1, i, static_cast<float>(0.4 * (2.0 * phaseB - 1.0)));
            }
            effect->process(block, n);
            for (int ch = 0; ch < 2; ch++) {
                all.copyFrom(ch, t * each + start, block, ch, 0, n);
            }
        }
    }
    checkGolden("effects", all);
}

TEST_CASE("Golden: Phase 4 machines, batch 1", "[golden]") {
    const juce::String types[] = { "Supersaw", "Pluck", "Organ", "FM" };
    for (const auto& type : types) {
        MachineRig rig(type);
        std::vector<Note> phrase { { 0.0, 1.5, 48, 0.9f }, { 0.0, 1.5, 55, 0.7f }, { 2.0, 1.0, 60, 0.8f }, { 3.0, 0.5, 67, 1.0f } };
        checkGolden(type.toLowerCase(), rig.play(phrase, 2.0));
    }
}
