// Pattern and song automation.

#include "Analysis.h"
#include "TestHelpers.h"
#include "core/SongFile.h"
#include "core/machines/SubtractiveSynth.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {
    constexpr double sampleRate = 48000.0;
    constexpr int bar = 96000;

    AutomationLane lane(AutomationTarget::Scope scope, const juce::String& id, std::vector<AutomationPoint> points, int machine = 0) {
        AutomationLane l;
        l.target.scope = scope;
        l.target.machine = machine;
        l.target.parameterId = id;
        l.points = std::move(points);
        return l;
    }

    // A steady sine on the Subtractive, so levels are easy to read.
    void steady(MachineRig& rig) {
        rig.set("osc1Shape", 3.0f);
        rig.set("oscMix", 0.0f);
        rig.set("cutoff", 20000.0f);
        rig.set("filterEnvAmount", 0.0f);
        rig.set("ampSustain", 1.0f);
    }
}

TEST_CASE("Automation interpolates between points and holds at the ends", "[automation]") {
    AutomationLane l = lane(AutomationTarget::Scope::machine, "x", {});
    CHECK(juce::exactlyEqual(l.valueAt(1.0), 0.0f));
    l.setPoint(2.0, 1.0f);
    l.setPoint(0.0, 0.0f);
    l.setPoint(4.0, 0.5f);
    REQUIRE(l.isSorted());
    CHECK(juce::exactlyEqual(l.valueAt(-1.0), 0.0f));
    CHECK(juce::exactlyEqual(l.valueAt(1.0), 0.5f));
    CHECK(juce::exactlyEqual(l.valueAt(3.0), 0.75f));
    CHECK(juce::exactlyEqual(l.valueAt(9.0), 0.5f));
    l.setPoint(2.0, 0.2f); // replaces
    CHECK(l.points.size() == 3);
    CHECK(juce::exactlyEqual(l.valueAt(2.0), 0.2f));
}

TEST_CASE("Pattern automation ramps a mixer parameter across the pattern", "[automation]") {
    MachineRig rig(SubtractiveSynth::typeName);
    steady(rig);
    Pattern pattern;
    pattern.notes = { { 0.0, 4.0, 69, 0.8f } };
    pattern.automation = { lane(AutomationTarget::Scope::mixer, "volume", { { 0.0, 0.1f }, { 4.0, 1.0f } }) };
    REQUIRE(rig.session.setPattern(rig.machine, 0, pattern));
    rig.session.setPlaying(true);
    const auto audio = rig.render(bar);

    const double early = Analysis::rms(audio, 0, 4800, 2400);
    const double late = Analysis::rms(audio, 0, bar - 7200, 2400);
    CHECK(late > 4.0 * early);
    // The document keeps the base value; automation only moves the live parameter.
    CHECK(juce::exactlyEqual(rig.session.getMixerParameter(rig.machine, "volume"), 0.8f));
}

TEST_CASE("Pattern automation runs from the pattern's own start, not the song's", "[automation]") {
    MachineRig rig(SubtractiveSynth::typeName);
    steady(rig);
    Pattern pattern;
    pattern.notes = { { 0.0, 4.0, 69, 0.8f } };
    pattern.automation = { lane(AutomationTarget::Scope::mixer, "volume", { { 0.0, 0.1f }, { 2.0, 0.1f }, { 2.0001, 1.0f } }) };
    REQUIRE(rig.session.setPattern(rig.machine, 0, pattern));
    rig.session.setBlocks(rig.machine, { { 1, 1, 0 } }); // the block starts at bar 2 (beat 4)
    rig.session.setSongMode(true);
    rig.session.setPlaying(true);
    const auto audio = rig.render(2 * bar);

    // Within the block: quiet for its first half, loud for its second.
    CHECK(Analysis::rms(audio, 0, bar + bar / 2 + 4800, 4800) > 5.0 * Analysis::rms(audio, 0, bar + 9600, 4800));
}

TEST_CASE("Pattern automation of a machine parameter follows the pattern position", "[automation]") {
    MachineRig rig(SubtractiveSynth::typeName);
    rig.set("osc1Shape", 0.0f); // saw
    rig.set("oscMix", 0.0f);
    rig.set("filterEnvAmount", 0.0f);
    rig.set("ampSustain", 1.0f);
    Pattern pattern;
    pattern.notes = { { 0.0, 4.0, 45, 0.8f } };
    pattern.automation = { lane(AutomationTarget::Scope::machine, "cutoff", { { 0.0, 200.0f }, { 2.0, 200.0f }, { 2.0001, 8000.0f } }) };
    REQUIRE(rig.session.setPattern(rig.machine, 0, pattern));
    rig.session.setPlaying(true);
    const auto audio = rig.render(bar);
    CHECK(Analysis::centroid(audio, bar * 3 / 4, sampleRate) > 3.0 * Analysis::centroid(audio, bar / 4, sampleRate));
}

TEST_CASE("Pattern automation can target an effect", "[automation]") {
    MachineRig rig(SubtractiveSynth::typeName);
    steady(rig);
    REQUIRE(rig.session.setEffect(rig.machine, 1, "Tremolo"));
    rig.session.setEffectParameter(rig.machine, 1, "depth", 0.0f);
    Pattern pattern;
    pattern.notes = { { 0.0, 4.0, 69, 0.8f } };
    AutomationLane depth = lane(AutomationTarget::Scope::effect, "depth", { { 0.0, 0.0f }, { 2.0, 0.0f }, { 2.0001, 1.0f } });
    depth.target.effectSlot = 1;
    pattern.automation = { depth };
    REQUIRE(rig.session.setPattern(rig.machine, 0, pattern));
    rig.session.setPlaying(true);
    const auto audio = rig.render(bar);

    auto wobble = [&](int from, int to) {
        double low = 1.0, high = 0.0;
        for (int start = from; start + 600 <= to; start += 600) {
            const double level = Analysis::rms(audio, 0, start, 600);
            low = std::min(low, level);
            high = std::max(high, level);
        }
        return high / juce::jmax(1.0e-6, low);
    };
    CHECK(wobble(4800, bar / 2 - 2400) < 1.2); // tremolo off for the first half
    CHECK(wobble(bar / 2 + 4800, bar) > 3.0);  // fully on for the second
}

TEST_CASE("Song automation moves the master in song mode only", "[automation]") {
    MachineRig rig(SubtractiveSynth::typeName);
    steady(rig);
    Pattern pattern;
    pattern.notes = { { 0.0, 4.0, 69, 0.8f } };
    rig.session.setPattern(rig.machine, 0, pattern);
    rig.session.setBlocks(rig.machine, { { 0, 2, 0 } });
    REQUIRE(rig.session.setSongAutomation({ lane(AutomationTarget::Scope::master, "volume", { { 0.0, 0.8f }, { 4.0, 0.8f }, { 4.0001, 0.1f } }) }));

    rig.session.setSongMode(true);
    rig.session.setPlaying(true);
    const auto song = rig.render(2 * bar);
    CHECK_THAT(Analysis::rms(song, 0, bar + 9600, 9600) / Analysis::rms(song, 0, 9600, 9600), WithinRel(0.125, 0.05));

    // In pattern mode the lane does nothing (master volume was left at its automated value: reset it).
    rig.session.setPlaying(false);
    rig.render(48000);
    rig.session.setMasterParameter("volume", 0.8f);
    rig.session.setSongMode(false);
    rig.session.setPlaying(true);
    const auto patternMode = rig.render(2 * bar);
    CHECK_THAT(Analysis::rms(patternMode, 0, bar + 9600, 9600) / Analysis::rms(patternMode, 0, 9600, 9600), WithinRel(1.0, 0.05));
}

TEST_CASE("Song automation follows machines when one is removed", "[automation][session]") {
    Rig rig;
    rig.session.addMachine(SubtractiveSynth::typeName);
    rig.session.addMachine(SubtractiveSynth::typeName);
    rig.session.addMachine(SubtractiveSynth::typeName);
    REQUIRE(rig.session.setSongAutomation({
        lane(AutomationTarget::Scope::mixer, "pan", { { 0.0, -1.0f } }, 0),
        lane(AutomationTarget::Scope::machine, "cutoff", { { 0.0, 500.0f } }, 1),
        lane(AutomationTarget::Scope::mixer, "volume", { { 0.0, 0.5f } }, 2),
        lane(AutomationTarget::Scope::master, "volume", { { 0.0, 0.7f } }),
    }));
    REQUIRE(rig.session.removeMachine(1));

    const auto& lanes = rig.session.getSongAutomation();
    REQUIRE(lanes.size() == 3);
    CHECK(lanes[0].target.machine == 0);
    CHECK(lanes[1].target.parameterId == "volume");
    CHECK(lanes[1].target.machine == 1); // was machine 2
    CHECK(lanes[2].target.scope == AutomationTarget::Scope::master);
}

TEST_CASE("Invalid automation is refused", "[automation][session]") {
    MachineRig rig(SubtractiveSynth::typeName);
    CHECK_FALSE(rig.session.setSongAutomation({ lane(AutomationTarget::Scope::mixer, "pan", { { 0.0, 0.0f } }, 5) }));
    AutomationLane unsorted = lane(AutomationTarget::Scope::mixer, "pan", { { 2.0, 0.0f }, { 1.0, 0.0f } });
    CHECK_FALSE(rig.session.setSongAutomation({ unsorted }));

    Pattern withMaster;
    withMaster.automation = { lane(AutomationTarget::Scope::master, "volume", { { 0.0, 0.0f } }) };
    CHECK_FALSE(rig.session.setPattern(rig.machine, 0, withMaster)); // pattern lanes stay in their machine
}

TEST_CASE("Automation is saved and loaded", "[automation][songfile]") {
    Rig first;
    const int m = first.session.addMachine(SubtractiveSynth::typeName);
    Pattern pattern;
    pattern.automation = { lane(AutomationTarget::Scope::machine, "cutoff", { { 0.0, 100.0f }, { 3.5, 5000.0f } }) };
    AutomationLane effectLane = lane(AutomationTarget::Scope::effect, "mix", { { 1.0, 0.25f } });
    effectLane.target.effectSlot = 1;
    pattern.automation.push_back(effectLane);
    REQUIRE(first.session.setPattern(m, 5, pattern)); // a pattern with only automation is still saved
    REQUIRE(first.session.setSongAutomation({ lane(AutomationTarget::Scope::master, "eq1Gain", { { 0.0, -3.0f }, { 16.0, 3.0f } }) }));

    juce::MemoryOutputStream out;
    REQUIRE(first.session.saveToStream(out).wasOk());
    Rig second;
    juce::MemoryInputStream in(out.getData(), out.getDataSize(), false);
    REQUIRE(second.session.loadFromStream(in).wasOk());
    CHECK(second.session.getPattern(0, 5) == pattern);
    CHECK(second.session.getSongAutomation() == first.session.getSongAutomation());

    Song song;
    juce::MemoryInputStream bad(juce::String("<NatronSong version=\"4\"><Automation scope=\"nowhere\" id=\"x\"/></NatronSong>").toRawUTF8(), 64, false);
    CHECK(SongFile::load(bad, song).getErrorMessage().contains("scope"));
}
