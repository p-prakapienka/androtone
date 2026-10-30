// Autosave: the song survives the app being backgrounded, killed, and relaunched.

#include "TestHelpers.h"
#include "core/Autosave.h"
#include "core/DemoSong.h"

#include <catch2/catch_test_macros.hpp>

namespace {
    juce::String songText(const Session& session) {
        juce::MemoryOutputStream stream;
        REQUIRE(session.saveToStream(stream).wasOk());
        return stream.toString();
    }

    struct TemporaryFolder {
        juce::File folder = juce::File::createTempFile("natron-autosave");
        ~TemporaryFolder() { folder.deleteRecursively(); }
    };
}

TEST_CASE("Autosave restores the song in a fresh session", "[autosave]") {
    TemporaryFolder temporary;
    const auto file = temporary.folder.getChildFile("Natron").getChildFile("autosave.natron");

    Rig before;
    REQUIRE(before.session.loadSong(makeDemoSong()).wasOk());
    before.session.setTempo(97.0);
    REQUIRE(Autosave::save(before.session, file).wasOk()); // creates the missing folders
    REQUIRE(file.existsAsFile());

    Rig after;
    REQUIRE(Autosave::restore(after.session, file).wasOk());
    CHECK(juce::exactlyEqual(after.session.getSong().tempo, 97.0));
    CHECK(songText(after.session) == songText(before.session));
}

TEST_CASE("Autosave replaces the previous copy", "[autosave]") {
    TemporaryFolder temporary;
    const auto file = temporary.folder.getChildFile("autosave.natron");

    Rig rig;
    REQUIRE(rig.session.loadSong(makeDemoSong()).wasOk());
    rig.session.setTempo(100.0);
    REQUIRE(Autosave::save(rig.session, file).wasOk());
    rig.session.setTempo(150.0);
    REQUIRE(Autosave::save(rig.session, file).wasOk());

    Rig restored;
    REQUIRE(Autosave::restore(restored.session, file).wasOk());
    CHECK(juce::exactlyEqual(restored.session.getSong().tempo, 150.0));
    CHECK(temporary.folder.getNumberOfChildFiles(juce::File::findFiles) == 1); // no temporary left behind
}

TEST_CASE("A missing or damaged autosave leaves the session alone", "[autosave]") {
    TemporaryFolder temporary;
    REQUIRE(temporary.folder.createDirectory().wasOk());
    const auto file = temporary.folder.getChildFile("autosave.natron");

    Rig rig;
    REQUIRE(rig.session.loadSong(makeDemoSong()).wasOk());
    const auto original = songText(rig.session);

    CHECK(Autosave::restore(rig.session, file).failed());
    REQUIRE(file.replaceWithText("<NatronSong version=\"4\"><Machines"));
    CHECK(Autosave::restore(rig.session, file).failed());
    CHECK(songText(rig.session) == original);
}

TEST_CASE("A new empty song loads and round-trips through autosave", "[autosave]") {
    TemporaryFolder temporary;
    const auto file = temporary.folder.getChildFile("autosave.natron");

    Rig rig;
    REQUIRE(rig.session.loadSong(makeDemoSong()).wasOk());
    REQUIRE(rig.session.loadSong(Song {}).wasOk()); // Settings > New song
    CHECK(rig.session.getNumMachines() == 0);
    REQUIRE(Autosave::save(rig.session, file).wasOk());

    Rig restored;
    REQUIRE(restored.session.loadSong(makeDemoSong()).wasOk());
    REQUIRE(Autosave::restore(restored.session, file).wasOk());
    CHECK(restored.session.getNumMachines() == 0);
}
