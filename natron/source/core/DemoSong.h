#pragma once

#include "Song.h"
#include "machines/AcidBass.h"
#include "machines/DrumMachine.h"
#include "machines/SubtractiveSynth.h"

// The app's starting song: drums, an acid line, and a pad. Each machine has a variation on A02.
inline Song makeDemoSong() {
    Song song;
    song.tempo = 124.0;

    // Drums: kick 36, snare 37, closed hat 38, open hat 39, clap 40.
    MachineState drums;
    drums.type = DrumMachine::typeName;
    drums.name = "Drums";
    auto& beat = drums.patterns[0];
    for (int step = 0; step < 16; step++) {
        const double start = step * Pattern::stepLength;
        if (step % 4 == 0) {
            beat.notes.push_back({ start, 0.25, 36, 1.0f });
        }
        if (step % 8 == 4) {
            beat.notes.push_back({ start, 0.25, 40, 0.9f });
        }
        if (step % 4 == 2) {
            beat.notes.push_back({ start, 0.25, 39, 0.6f });
        } else {
            beat.notes.push_back({ start, 0.25, 38, step % 2 == 0 ? 0.7f : 0.45f });
        }
    }
    auto& fill = drums.patterns[1];
    fill = beat;
    for (int step = 12; step < 16; step++) {
        fill.notes.push_back({ step * Pattern::stepLength, 0.25, 37, 0.5f + 0.1f * static_cast<float>(step - 12) });
    }
    drums.mixer["volume"] = 0.9f;
    song.machines.push_back(drums);

    // Acid: 16 steps; velocity >= 0.9 accents, overlapping notes slide.
    MachineState acid;
    acid.type = AcidBass::typeName;
    acid.name = "Acid";
    struct Step { int step; int pitch; float velocity; double length; };
    const Step line[] = {
        { 0, 36, 1.0f, 0.2 }, { 2, 36, 0.7f, 0.2 }, { 3, 48, 0.7f, 0.2 }, { 4, 36, 0.95f, 0.2 },
        { 6, 39, 0.7f, 0.5 }, { 8, 41, 0.7f, 0.2 }, // 39 overlaps 41: slide
        { 10, 36, 1.0f, 0.2 }, { 11, 48, 0.7f, 0.2 }, { 12, 46, 0.7f, 0.3 }, { 13, 43, 0.95f, 0.2 },
        { 14, 36, 0.7f, 0.2 },
    };
    for (const auto& s : line) {
        acid.patterns[0].notes.push_back({ s.step * Pattern::stepLength, s.length, s.pitch, s.velocity });
    }
    acid.patterns[1] = acid.patterns[0];
    for (auto& note : acid.patterns[1].notes) {
        note.pitch += 3;
    }
    acid.mixer["delaySend"] = 0.2f;
    acid.mixer["volume"] = 0.7f;
    song.machines.push_back(acid);

    // Pad: two-bar chords on a soft, slow Subtractive patch.
    MachineState pad;
    pad.type = SubtractiveSynth::typeName;
    pad.name = "Pad";
    pad.parameters = {
        { "osc1Shape", 0.0f }, { "osc2Shape", 0.0f }, { "osc2Fine", 12.0f }, { "cutoff", 1400.0f },
        { "filterEnvAmount", 0.2f }, { "ampAttack", 0.4f }, { "ampRelease", 0.8f }, { "level", 0.18f },
    };
    auto& chords = pad.patterns[0];
    chords.bars = 2;
    const int voicings[2][3] = { { 60, 63, 67 }, { 58, 62, 65 } };
    for (int bar = 0; bar < 2; bar++) {
        for (const int pitch : voicings[bar]) {
            chords.notes.push_back({ bar * 4.0, 3.75, pitch, 0.7f });
        }
    }
    pad.mixer["reverbSend"] = 0.5f;
    pad.mixer["width"] = 1.4f;
    song.machines.push_back(pad);

    return song;
}
