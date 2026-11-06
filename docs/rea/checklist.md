# Live-style workflow checklist

Cliptone implements the clip-based workflow described in public DAW documentation (the
Ableton Live 12 manual), as an original program. Nothing is decompiled or copied: the manual is
read for **behaviour only**, paraphrased here, and every implementation decision is our own.

This file follows the REA investigation loop (morluto/rea, "Plan broader investigations"),
applied to a clean-room build instead of a binary:

1. Turn the request into a checklist of questions, each naming the evidence it needs.
2. Reuse existing state; start from the smallest overview.
3. Corroborate each conclusion with the evidence type it requires — here, a documented behaviour
   (manual §) and an automated test or a screenshot check in Cliptone.
4. Keep a ledger (`ledger.md`) separating documented behaviour, design choices and unknowns.
5. Before finishing, revisit this list and mark each item **done**, **partial** or **open**.

Evidence tags: `§n.n` = Live 12 manual section (paraphrased). `T:` = test in
`tests/SequencerTests.cpp`. `S:` = checked by screenshot of the running standalone.

Scope: MIDI only (no audio clips / warping), touch-first UI (keyboard shortcuts out of scope),
one window. Status is updated as milestones land.

## A. Session view and clip launching

| ID  | Must-have                                                                                       | Evidence        | Verify | Status |
| --- | ----------------------------------------------------------------------------------------------- | --------------- | ------ | ------ |
| A1  | Launch quantization: None, 1/16 … 8 bars; queued launches wait for the boundary                  | launching-clips | T      | done   |
| A2  | Launch modes Trigger / Gate / Toggle / Repeat                                                    | launching-clips | T      | done   |
| A3  | Per-track clip stop, stop-all, empty slot stops track                                            | §7.5            | T      | done   |
| A4  | Scene launch launches the whole row                                                              | §7.1            | T      | done   |
| A5  | Follow actions A/B with chance, linked/unlinked time, group semantics                            | launching-clips | T      | done   |
| A6  | Launching a clip starts the transport                                                            | §7.1, §19.4     | T      | done   |
| A7  | New empty MIDI clip in an empty slot = 1 bar, looped                                             | §19.3.3         | T, S   | done   |
| A8  | Legato launch, jump follow action, per-clip quantization, global follow switch (scene follow actions: open) | launching-clips | T | partial |

## B. Clips and the note editor

| ID  | Must-have                                                                                       | Evidence        | Verify | Status |
| --- | ----------------------------------------------------------------------------------------------- | --------------- | ------ | ------ |
| B1  | Clips hold free notes (start, length, pitch, velocity); chords and notes longer than a step      | §10.1           | T      | done   |
| B2  | Start/end markers; unlooped clip plays start→end once then stops                                 | §8.2.1, §8.8    | T      | done   |
| B3  | Loop on/off with loop start/end; playback may start before the loop ("run into loop")            | §8.9            | T      | done   |
| B4  | Clip name and colour (default = track colour)                                                    | §8.1.1.2–3      | S      | done   |
| B5  | Note editor: pitch × time grid, add / delete / move / resize notes with grid snapping            | §10.4–10.5.5    | S      | done   |
| B6  | Grid sizes selectable; snapping can be turned off                                                | §10.2.1         | S      | done   |
| B7  | Velocity lane below the grid; note shade shows velocity                                          | §10.1, §10.5.12 | S      | done   |
| B8  | Quantize notes to grid with amount %                                                             | §10.5.11        | T      | done   |
| B9  | Transpose selection / whole clip (semitones, octave)                                             | §10.5.9.1       | T      | done   |
| B10 | Duplicate loop (doubles loop and copies its notes)                                               | §8.9            | T      | done   |
| B11 | Fold: show only rows that contain notes                                                          | §10.6           | S      | done   |
| B12 | Mute (deactivate) individual notes                                                               | §10.5.7         | T      | done   |
| B13 | Overlap rule: new/moved note over another's start replaces it, over its end shortens it          | §10.5.4         | T      | done   |

## C. Transport and tempo

| ID  | Must-have                                                                                       | Evidence        | Verify | Status |
| --- | ----------------------------------------------------------------------------------------------- | --------------- | ------ | ------ |
| C1  | Position display in bars.beats.sixteenths, starting at 1.1.1                                     | §6.3, §19.6     | S      | done   |
| C2  | Tempo field with hundredths of a BPM                                                             | §9.1.1          | S      | done   |
| C3  | Tap tempo: each tap updates tempo, more taps average better                                      | §9.1.2          | T      | done   |
| C4  | Metronome on/off, ticks while playing, accented downbeat                                         | §19.4           | T      | done   |
| C5  | Count-in (None/1/2/4 bars) before recording, shown as negative position                          | §19.6           | T      | done   |
| C6  | Groove: global swing on off-beat 16ths with timing and velocity amounts                          | §14.1.1         | T      | done   |
| C7  | Follow host tempo and transport when running as a plugin                                         | (design)        | T      | done   |
| C8  | Stop twice returns to 1.1.1                                                                      | §7.1            | T      | done   |

## D. Mixer

| ID  | Must-have                                                                                       | Evidence        | Verify | Status |
| --- | ----------------------------------------------------------------------------------------------- | --------------- | ------ | ------ |
| D1  | Volume, pan (double-tap resets to centre)                                                        | §18.1           | T, S   | done   |
| D2  | Track activator (mute)                                                                           | §18.1           | T      | done   |
| D3  | Solo: exclusive by default, multi-solo optional; returns stay audible                            | §18.1, §18.6    | T      | done   |
| D4  | Arm: exclusive by default                                                                        | §18.1           | T      | done   |
| D5  | Peak meters per track and on main                                                                | §18.1           | S      | done   |
| D6  | Return tracks with per-track sends; pre/post per return                                          | §18.4           | T      | done   |

## E. Recording

| ID  | Must-have                                                                                       | Evidence        | Verify | Status |
| --- | ----------------------------------------------------------------------------------------------- | --------------- | ------ | ------ |
| E1  | Armed tracks show record buttons in empty slots; recording creates a clip there                  | §19.3.2         | T, S   | done   |
| E2  | Session record button records into armed tracks; pressing again switches to playback             | §19.3.2         | T      | done   |
| E3  | Overdub: notes layer on each loop pass of a playing clip                                         | §19.3.3         | T      | done   |
| E4  | Record quantization snaps incoming notes                                                         | §19.5           | T      | done   |
| E5  | Live input monitoring through armed track's instrument                                           | §18.1 (arm)     | T      | done   |

## F. Arrangement

| ID  | Must-have                                                                                       | Evidence        | Verify | Status |
| --- | ----------------------------------------------------------------------------------------------- | --------------- | ------ | ------ |
| F1  | Timeline of clip instances per track; plays from the insert marker                              | §6.3            | T      | done   |
| F2  | Loop brace on/off with start/length                                                              | §6.6            | T      | done   |
| F3  | Arrangement record logs session launches as arrangement clips at their song positions           | §7.5, §19.3.1   | T      | done   |
| F4  | Launching a session clip on a track overrides its arrangement; Back to Arrangement restores      | §7.5            | T      | done   |
| F5  | Edit arrangement clips: move, resize, delete, duplicate                                          | §6.9            | S      | done   |
| F6  | Chase MIDI notes when starting mid-note                                                          | §6.3            | T      | done   |

## G. Sets, undo and devices

| ID  | Must-have                                                                                       | Evidence        | Verify | Status |
| --- | ----------------------------------------------------------------------------------------------- | --------------- | ------ | ------ |
| G1  | Save/load the whole set: clips, settings, mixer, arrangement, device params                     | §5.4.1, §3.21   | T      | done   |
| G2  | Host session state (plugin state) restores the set                                               | (design)        | T      | done   |
| G3  | Undo/redo of edits; history not saved with the set                                               | §5.4.2          | T      | done   |
| G4  | Export a clip as a standard MIDI file                                                            | §5.2            | T      | done   |
| G5  | Instrument device with editable parameters per track (synth, drum kit)                           | §3 (devices)    | S      | done   |
| G6  | Track insert effects (filter, compressor)                                                        | §3 (devices)    | T      | done   |
| G7  | Automation of mixer parameters in the arrangement; manual override + re-enable                   | §25.1, §25.4    | T      | done   |

## Out of scope (recorded so the boundary is explicit)

Audio clips, warping, Link / MIDI sync, groove files and extraction, MIDI tools beyond quantize /
transpose, keyboard shortcut parity, cue outputs, group tracks, MPE, tuning systems, scales.

## Final review (REA step 5)

Every item above was revisited against its evidence: 52 **done**, 1 **partial**, 0 **open**.

- Each "T" item is covered by `tests/SequencerTests.cpp` (headless; build with
  `-DCLIPTONE_BUILD_TESTS=ON`, run `CliptoneTests`). All checks pass on Linux / GCC.
- Each "S" item was checked by screenshots of the standalone build under Xvfb.
- **A8 partial:** scene follow actions are not implemented. Song mode (loop button off) covers
  chaining scenes in order.

Bounded, not verified here:

- Audio output was never listened to. The test machine has no audio device, so sound checks
  are numeric: levels, decay and event timing in samples.
- The Android build was not built or run. Only the desktop VST3 / Standalone targets were
  compiled. The `.jucer` file list was kept in sync, so re-save it in Projucer before exporting.
- No plugin-host session was run. Host sync and state save/restore are covered by tests at the
  sequencer / serializer level, not inside a real DAW.
- Not implemented (outside the checklist): automation recording, clip envelopes, group tracks,
  audio clips. See "Out of scope" above.
