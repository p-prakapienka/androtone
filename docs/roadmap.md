# Roadmap: from the current sequencer to a Note-style Android app

How to get from what Androtone is today (commit 17, "add delay send") to the feature set in
`note-reference-spec.md`. Milestones keep the repo's numbered-commit style.

## How we build it

A known reference app makes cloning fast for two reasons. The reference already settles what
each feature should do, and the work can be checked automatically instead of by eye. Note runs
only on iOS and can't be scripted, so we can't diff our output against it directly. Instead:

1. **The spec is the requirement.** Each milestone names the rows of `note-reference-spec.md`
   it covers. Behaviour that isn't in the spec is a design decision: write it into `docs/`
   before building it.
2. **Every milestone ships with an automatic check.** Use headless unit tests for timing and
   model logic, offline "golden" renders for audio, and save/load round trips for persistence.
   An agent can run the write → test → fix loop without a person checking each step.
3. **Hand-check feel against the real app.** Latency, pad response and editor gestures need
   someone to compare Note on an iPhone with Androtone on Android, side by side. Do that once
   per phase, not once per commit.
4. **Foundations first, then parallel streams.** Phase 0 settles the shared interfaces (clock,
   clip model, document model). After that the instrument, pad, editor and export work touch
   different files and can be built in parallel.

## Where the code is today

| Area | Current state | Gap to Note |
| --- | --- | --- |
| Clip model (`Sequencer/Clip.h`) | One note per 16th-note step; the step index is the note's position; `ProjectNote::start` is ignored on load | Needs polyphonic, off-grid notes (recorded, nudged, triplets), clip length in bars up to 16, time signatures |
| Timing | Each clip counts its own samples; bar = hard-coded 16 steps; launches wait for the playing clip's bar end | Needs one transport clock (song position in beats) and launch quantisation independent of clip content. Ableton Link would need this too |
| Scenes (`SceneManager.h`) | Scene launch and song-mode chaining; current scene is read from track 0 | Mostly there. Needs per-scene state independent of track 0, and scene add/duplicate/move/delete |
| Tracks | 8 tracks, 32 clip slots; instruments hard-wired in the processor constructor (kick sampler, saw, sine); tracks 4–8 are silent | Needs a track type (drum / melodic / audio) and an instrument picked per track |
| Instruments | Sine, saw, one-shot sample player (JUCE `Synthesiser`) | Needs a 16-pad drum kit, a usable poly synth with presets, a melodic sampler, mic sampling |
| Mixer (`Mixer/Mixer.h`) | Per-track volume, delay and reverb sends, master volume | Needs mute, solo and pan, per-track insert effects, meters |
| Effects | Reverb and delay as send returns, processed per sample | Needs the Note effect list as per-track inserts; switch to block processing |
| Thread safety | The UI calls straight into audio objects; `Clip::setNotes` reallocates a vector the audio thread reads | Editing, recording and undo need a UI-side document plus real-time-safe snapshots for the audio thread |
| Persistence | `get/setStateInformation` are empty; projects are compiled in (`Projects.h`) | Needs set save/load and a set browser |
| Input | Clip and scene launch buttons only | Needs pads, keys, recording, Repeat, Capture, external MIDI |
| UI | Fixed 500×450 editor with tabs | Needs a touch-first phone layout, portrait and landscape |
| Tests | None | Needs a headless test target and CI (phase 0) |

## Phase 0: foundations (sequential)

These change the core types every later stream depends on, so they land one after another.

- **18. Test harness.** Add an `AndrotoneTests` console target to the desktop CMake (JUCE
  `UnitTestRunner` or Catch2 via CPM) and an offline renderer: given a `Project`, render N bars
  to a buffer at a fixed sample rate. Run it in CI on Linux. Check: CI runs green and a
  known-good render matches its stored golden file.
- **19. Transport clock and launch quantisation.** One song position in beats owned by the
  sequencer. Clips play relative to their launch point. Launch quantisation (none, 1/4, 1/2,
  1 bar, 2 bars) is a set setting rather than "the end of the playing clip's bar". Check: unit
  tests assert sample-exact note-on positions for launch, scene change and tempo change mid-bar.
- **20. Event-based clips.** A clip holds a list of `{start, length, note, velocity}` in beats,
  sorted, plus a loop length in bars, so several notes can share a time and notes can sit off
  the grid. Migrate `Projects.h` and drop the zero-velocity padding hack. Check: chords, triplets
  and a 16-bar clip render correctly; the existing preset renders unchanged against its golden.
- **21. Document model, real-time-safe hand-off, undo.** The UI edits a document
  (`juce::ValueTree`, or the `Project` struct with commands). Every change publishes an immutable
  snapshot to the audio thread by atomic pointer swap, with nothing allocated or locked on the
  audio thread. `juce::UndoManager` provides undo. Check: an editing stress test while rendering
  passes under ThreadSanitizer; undo and redo round-trip the document.
- **22. Set save/load.** Serialise sets (ValueTree XML or JSON) into app storage; add a set
  list in the Projects tab; implement `get/setStateInformation` for the VST3 build. Check:
  save → load → render matches the original render bit for bit.

## Phase 1+: parallel streams

After phase 0 these streams touch mostly separate files. Each item lists its spec rows and its
check.

**A. Instruments and sound** (`Engine/SoundSources/`)
- 23. Instrument interface and per-track track type / instrument choice (spec §2).
- 24. Drum kit: 16 pads with one sample each, choke groups, per-pad level, pitch and decay;
  "16 Pitches" mode (spec §2). Check: golden render of a kit pattern.
- 25. Poly synth: 2 oscillators, filter, amp and filter envelopes, LFO, preset format and
  browser (spec §2). Check: golden renders per preset; CPU budget measured on a mid-range phone.
- 26. Mic sampling into a pad or a melodic sampler: trim, filter, repitch; the Drum Sampler
  FX types (spec §2, §3). Check: render of a fixture recording.

**B. Playing and recording** (`Ui/`, sequencer input path)
- 27. Pad surface: drum 4×4; melodic pads with key, scale and In Key, Fourths and Sequential
  layouts; piano keys (spec §3). Check: unit tests for the pad → MIDI note mapping per layout
  and scale.
- 28. Recording: arm, count-in, metronome, overdub into a clip, record-quantise (spec §3).
  Check: injected MIDI at known sample offsets lands on the expected beats.
- 29. Repeat and Capture (rolling input buffer → clip) (spec §3). Check: unit tests.
- 30. External MIDI controllers over USB and BLE on Android (spec §3, §7).

**C. Editing** (`Ui/`, document commands)
- 31. MIDI editor: piano roll / step grid, add, delete, move and resize notes, velocity,
  multi-select, nudge, quantise, triplet grid, copy/paste (spec §4). Check: command-level unit
  tests on the document model.
- 32. Clip and scene operations: add, duplicate, move, delete, set clip length (spec §1).

**D. Mix and effects** (`Mixer/`, `Engine/Processors/`)
- 33. Mixer mute, solo, pan and meters; move effects to block processing (spec §5).
- 34. Per-track insert chain with EQ, Chorus, Phaser/Flanger, Redux (bit/rate crush) and
  Saturator (spec §5). Check: golden render per effect.

**E. Output and platform** (`Builds/android/`, export code)
- 35. Export: WAV mixdown, per-track stems and Standard MIDI File through the Android share
  sheet (spec §6, §7). Check: exported WAV equals the offline render.
- 36. Android audio: confirm the Oboe/AAudio low-latency path, measure round-trip latency,
  handle background and audio-focus lifecycle, phone layout in portrait and landscape.

## Phase later: Note 2.x parity

Audio tracks and the audio clip editor with warp markers, MIDI → audio bounce, arbitrary time
signatures, Erosion / Auto Shift / Auto Pan, and Link. Plan these once phases 0–1 are in and the
2.x behaviour has been checked against the real app.

## Practical notes for parallel work

- Every new source file has to be added to both `androtone.jucer` and
  `Builds/desktop/CMakeLists.txt` (see `CLAUDE.md`). Parallel branches will conflict on those two
  lists. Merge streams often, or have one integrator own those files.
- Keep each stream's check in the CI test target, so a merge that breaks another stream fails
  straight away instead of at the next hand test.

## Open decisions (owner: project lead)

1. **Target version:** match Note 1.x (MIDI only) first, or plan audio tracks (2.x) from the
   start? This changes the track model in milestone 23.
2. **Live interop:** stop at WAV/stems/MIDI export, or attempt Ableton `.als` export?
   The format is undocumented, so `.als` export has clean-room risk.
3. **Ableton Link:** the SDK is GPL-2.0+ unless Ableton grants a commercial licence. Using it
   would make the app GPL.
4. **Desktop targets:** keep VST3 and desktop Standalone as development and test vehicles
   (recommended: the test harness runs there), or drop them?
5. **Sound content:** where kits and presets come from (made in-house, CC0 libraries, licensed).
