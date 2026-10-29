// Song file format: round trips and rejection of bad files.

#include "TestHelpers.h"
#include "core/DemoSong.h"
#include "core/SongFile.h"

#include <catch2/catch_test_macros.hpp>

namespace {
    juce::String toXml(const Song& song) {
        juce::MemoryOutputStream stream;
        REQUIRE(SongFile::save(song, stream).wasOk());
        return stream.toString();
    }

    juce::Result fromXml(const juce::String& xml, Song& song) {
        juce::MemoryInputStream stream(xml.toRawUTF8(), xml.getNumBytesAsUTF8(), false);
        return SongFile::load(stream, song);
    }

    void checkEqual(const Song& a, const Song& b) {
        CHECK(juce::exactlyEqual(a.tempo, b.tempo));
        CHECK(a.master == b.master);
        REQUIRE(a.machines.size() == b.machines.size());
        for (size_t m = 0; m < a.machines.size(); m++) {
            CAPTURE(m);
            const auto& x = a.machines[m];
            const auto& y = b.machines[m];
            CHECK(x.type == y.type);
            CHECK(x.name == y.name);
            CHECK(x.mixer == y.mixer);
            CHECK(x.selectedPattern == y.selectedPattern);
            CHECK(x.parameters == y.parameters);
            for (size_t p = 0; p < x.patterns.size(); p++) {
                CAPTURE(p);
                CHECK(x.patterns[p] == y.patterns[p]);
            }
        }
    }
}

TEST_CASE("A song survives a save/load round trip", "[songfile]") {
    auto song = makeDemoSong();
    song.master["volume"] = 0.65f;
    song.master["delayFeedback"] = 0.3f;
    song.machines[1].mixer["pan"] = -0.25f;
    song.machines[0].selectedPattern = 1;
    song.machines[0].parameters["level"] = 0.33f;
    song.machines[1].patterns[63].bars = 8;
    song.machines[1].patterns[63].notes.push_back({ 1.0 / 3.0, 0.1, 127, 0.123f }); // awkward values

    Song loaded;
    REQUIRE(fromXml(toXml(song), loaded).wasOk());
    checkEqual(song, loaded);
}

TEST_CASE("A session saves and reloads the same song", "[songfile][session]") {
    Rig first;
    REQUIRE(first.session.loadSong(makeDemoSong()).wasOk());
    first.session.setParameter(2, "ampRelease", 1.5f);
    first.session.setMixerParameter(0, "eqLow", 4.0f);
    first.session.setMasterParameter("reverbSize", 0.9f);
    first.session.selectPattern(0, 1);

    juce::MemoryOutputStream out;
    REQUIRE(first.session.saveToStream(out).wasOk());

    Rig second;
    juce::MemoryInputStream in(out.getData(), out.getDataSize(), false);
    REQUIRE(second.session.loadFromStream(in).wasOk());
    checkEqual(first.session.getSong(), second.session.getSong());
}

TEST_CASE("Empty patterns are not written", "[songfile]") {
    const auto xml = juce::parseXML(toXml(makeDemoSong()));
    REQUIRE(xml != nullptr);

    int written = 0;
    for (auto* machine : xml->getChildWithTagNameIterator("Machine")) {
        for (auto* pattern : machine->getChildWithTagNameIterator("Pattern")) {
            juce::ignoreUnused(pattern);
            written++;
        }
    }
    CHECK(written == 5); // drums A01 + A02, acid A01 + A02, pad A01
}

TEST_CASE("Bad files are rejected with a reason", "[songfile]") {
    Song song;

    CHECK(fromXml("this is not xml", song).getErrorMessage().contains("XML"));
    CHECK(fromXml("<SomethingElse/>", song).getErrorMessage().contains("Not a Natron song"));
    CHECK(fromXml("<NatronSong/>", song).getErrorMessage().contains("version"));
    CHECK(fromXml("<NatronSong version=\"0\"/>", song).getErrorMessage().contains("version"));
    CHECK(fromXml("<NatronSong version=\"99\"/>", song).getErrorMessage().contains("newer"));
    CHECK(fromXml("<NatronSong version=\"1\"><Machine type=\"TestTone\"><Pattern index=\"64\"/></Machine></NatronSong>", song)
              .getErrorMessage().contains("index"));
    CHECK(fromXml("<NatronSong version=\"1\"><Machine type=\"TestTone\"><Pattern index=\"0\" bars=\"3\"/></Machine></NatronSong>", song)
              .getErrorMessage().contains("length"));
}

TEST_CASE("A failed load leaves the song untouched", "[songfile]") {
    Song song = makeDemoSong();
    CHECK(fromXml("<NatronSong version=\"99\"/>", song).failed());
    CHECK(song.machines.size() == 3);
}

TEST_CASE("Missing attributes fall back to defaults and values are clamped", "[songfile]") {
    Song song;
    REQUIRE(fromXml("<NatronSong version=\"1\"><Machine type=\"TestTone\">"
                    "<Pattern index=\"3\" bars=\"4\"><Note pitch=\"300\" velocity=\"5\"/></Pattern>"
                    "</Machine></NatronSong>", song).wasOk());

    REQUIRE(song.machines.size() == 1);
    CHECK(juce::exactlyEqual(song.tempo, 120.0));
    const auto& pattern = song.machines[0].patterns[3];
    CHECK(pattern.bars == 4);
    REQUIRE(pattern.notes.size() == 1);
    CHECK(pattern.notes[0].pitch == 127);
    CHECK(juce::exactlyEqual(pattern.notes[0].velocity, 1.0f));
    CHECK(juce::exactlyEqual(pattern.notes[0].length, Pattern::stepLength));
}

TEST_CASE("Version 1 files are upgraded", "[songfile][migration]") {
    Song song;
    REQUIRE(fromXml("<NatronSong version=\"1\" tempo=\"100\" masterVolume=\"0.5\">"
                    "<Machine type=\"TestTone\" name=\"Old\" volume=\"0.3\">"
                    "<Parameter id=\"level\" value=\"0.4\"/></Machine></NatronSong>", song).wasOk());

    CHECK(juce::exactlyEqual(song.master.at("volume"), 0.5f));
    REQUIRE(song.machines.size() == 1);
    CHECK(juce::exactlyEqual(song.machines[0].mixer.at("volume"), 0.3f));
    CHECK(juce::exactlyEqual(song.machines[0].parameters.at("level"), 0.4f));

    // And they save as the current version.
    const auto xml = juce::parseXML(toXml(song));
    REQUIRE(xml != nullptr);
    CHECK(xml->getIntAttribute("version") == SongFile::currentVersion);
    CHECK_FALSE(xml->hasAttribute("masterVolume"));
}
