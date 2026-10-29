#pragma once

#include "Engine.h"
#include "machines/TestToneMachine.h"

// A two-machine loop for the app's first screen: an 8th-note bass line and sustained chords,
// which exercise polyphony and notes longer than a step.
inline void loadDemoSong(Engine& engine) {
    engine.setTempo(110.0);

    const int bass = engine.addMachine(std::make_unique<TestToneMachine>());
    auto& bassPattern = engine.getPattern(bass);
    bassPattern.bars = 2;
    const int bassLine[] = { 45, 45, 57, 45, 48, 45, 55, 43, 41, 41, 53, 41, 43, 43, 55, 47 };
    for (int i = 0; i < 16; i++) {
        bassPattern.notes.push_back({ i * 0.5, 0.4, bassLine[i], i % 4 == 0 ? 1.0f : 0.7f });
    }

    const int chords = engine.addMachine(std::make_unique<TestToneMachine>());
    auto& chordPattern = engine.getPattern(chords);
    chordPattern.bars = 2;
    const int chordNotes[2][3] = { { 69, 72, 76 }, { 65, 69, 72 } };
    for (int bar = 0; bar < 2; bar++) {
        for (const int pitch : chordNotes[bar]) {
            chordPattern.notes.push_back({ bar * 4.0, 3.5, pitch, 0.6f });
        }
    }
    engine.setMachineVolume(chords, 0.5f);
}
