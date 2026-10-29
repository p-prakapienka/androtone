#pragma once

#include "Song.h"
#include "machines/TestToneMachine.h"

// A two-machine loop for the app's first screen: an 8th-note bass line (A01, with a variation on
// A02) and sustained chords, which exercise polyphony and notes longer than a step.
inline Song makeDemoSong() {
    Song song;
    song.tempo = 110.0;

    MachineState bass;
    bass.type = TestToneMachine::typeName;
    bass.name = "Bass";
    auto& bassLine = bass.patterns[0];
    bassLine.bars = 2;
    const int bassPitches[] = { 45, 45, 57, 45, 48, 45, 55, 43, 41, 41, 53, 41, 43, 43, 55, 47 };
    for (int i = 0; i < 16; i++) {
        bassLine.notes.push_back({ i * 0.5, 0.4, bassPitches[i], i % 4 == 0 ? 1.0f : 0.7f });
    }
    auto& variation = bass.patterns[1];
    variation.bars = 1;
    for (int i = 0; i < 8; i++) {
        variation.notes.push_back({ i * 0.5, 0.2, i % 2 == 0 ? 45 : 57, 0.8f });
    }
    song.machines.push_back(bass);

    MachineState chords;
    chords.type = TestToneMachine::typeName;
    chords.name = "Chords";
    chords.volume = 0.5f;
    auto& chordPattern = chords.patterns[0];
    chordPattern.bars = 2;
    const int chordPitches[2][3] = { { 69, 72, 76 }, { 65, 69, 72 } };
    for (int bar = 0; bar < 2; bar++) {
        for (const int pitch : chordPitches[bar]) {
            chordPattern.notes.push_back({ bar * 4.0, 3.5, pitch, 0.6f });
        }
    }
    song.machines.push_back(chords);

    return song;
}
