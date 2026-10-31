# Roadmap

Target: `docs/spec.md` (Caustic 3, clean room). Each step should be small, land with tests, and
keep `NatronTests` green.

## Principles

1. **The spec is fixed — don't redesign.** When unsure how something behaves, check Caustic and
   write the answer into `docs/spec.md` before coding it.
2. **Every feature gets an automatic check.** Render offline (`tests/TestHelpers.h`) and assert on
   MIDI positions and audio. That is what lets work run in long write → test → fix loops.
3. **Interfaces first, then parallel work.** Once the machine, effect-slot, and parameter
   interfaces exist, each machine, effect, and screen is an independent work item.
4. **Lean on JUCE.** `juce::dsp` covers filters, oscillators, chorus, phaser, compressor, limiter,
   reverb, delay lines, and waveshapers; most effects are wiring, not DSP research.

## Phase 0 — skeleton and test harness ✅

- `natron/` app with its own desktop CMake (app + `NatronTests`) and a Projucer export for Android.
- Engine core with no GUI dependency: `Transport` (block-size-independent beat ↔ sample
  mapping, tempo re-anchoring), polyphonic `Pattern` (notes in beats, any length, 1/2/4/8 bars),
  `PatternPlayer` (sample-accurate MIDI, notes longer than a step or the loop, retrigger, off
  before on), `Machine` interface, `Engine` (rack of up to 14 machines, volume, master).
- Placeholder `TestToneMachine` and a demo song; minimal app shell (play/stop, tempo, position).
- Offline Catch2 tests (transport, player, engine) and Linux CI (`.github/workflows/natron.yml`).

## Phase 1 — core model ✅

- **Live editing:** `Session` (message thread) owns the `Song` document and posts `EngineCommand`s
  through a lock-free SPSC queue; replaced patterns and machines come back through a garbage queue
  and are freed on the message thread. A test counts heap allocations and frees inside
  `Engine::process` while edits stream in, and requires zero.
- **Parameters:** `ParameterSet` with `ParameterSpec` (stable id, range, default) and atomic values;
  machines declare theirs in the constructor. `MachineRegistry` creates machines by type name.
- **Pattern banks:** 64 patterns per machine (A01–D16). Selecting while playing queues the switch
  for the end of the current pattern; the new pattern loops from where it started; stopping
  applies a queued switch at once. (Switch timing still to verify against Caustic.)
- **Song files:** `SongFile` — versioned XML (`<NatronSong version="1">`), empty patterns omitted,
  bad files rejected with a reason, failed loads change nothing.
- **App:** rack view (bank selector + 16 pattern pads per machine, queued pads blink), save/load
  through the system file picker (works with Android content URIs).

Deferred: parameter smoothing inside notes (lands with the first real synth in Phase 2).

## Phase 2 — first playable loop ✅

- **Machines:** `Subtractive` (SubSynth), `Acid` (BassLine, accent + slide), `Drums` (BeatBox, synthesised
  kit). See the table in `docs/spec.md`.
- **Mixer:** `ChannelStrip` per machine — volume, pan, width, 3-band EQ (250 Hz shelf, 1.2 kHz peak, 4 kHz
  shelf), delay/reverb sends, mute/solo. Channels are created and freed with their machine, so their
  settings move with it on removal (fixes the Phase 1 volume race). `MasterBus` — tempo-synced delay,
  reverb, volume, peak limiter (ceiling always holds).
- **Song format v2:** `<Mixer>` and `<Master>` values; v1 files are upgraded on load.
- **UI:** Rack tab (select a machine to see its controls; add/remove machines), Mixer tab (every
  channel plus master). Controls are generated from each `ParameterSet`.
- **Tests:** behaviour tests per machine and for the mixer (pitch, filter, slide, accent, pan, solo, EQ,
  echo timing, limiter); fingerprint tests (`tests/golden/`, RMS + spectral centroid per 50 ms) for each
  machine and the demo song; the allocation test now streams machine, mixer, and master edits too.

Deferred: master parametric EQ (Phase 4 with the other effects); importing samples into Drums (Phase 4).

## Phase 3 — arrangement and editing ✅

- **Song mode:** each machine has a timeline row of pattern blocks (`Arrangement.h`: place / move /
  resize / remove, overlaps trimmed or split). The engine plays blocks bar by bar, the pattern
  looping from its block's start; the loop region repeats (playback starts at its start), else
  the song stops at its end. Pattern mode is unchanged and keeps the user's selection.
- **Insert effects:** two slots per machine, between the machine and its channel: Distortion,
  Bitcrusher, Chorus, Flanger, Phaser, Compressor, Filter, Parametric EQ (`effects/`).
- **WAV export:** `SongExport` renders the song (or N bars of the selected patterns) on a private
  engine — never the live one — then writes 16/24-bit WAV. Runs on a background thread in the app.
- **Editors:** Pattern tab (touch piano roll; Drums show as a step grid; long-press cycles
  velocity default → accent → soft; drag an edge to resize, overlap for Acid slides; pinch zoom)
  and Song tab (timeline with loop ruler). Editing logic lives in `PatternEditing.h` and
  `Arrangement.h`, both unit-tested.
- **Song format v3:** effects, blocks, song mode and loop settings.

Deferred: undo/redo; copy/paste of patterns and blocks; per-step automation (Phase 4).

## Phase 4 — breadth ✅

- **4a infrastructure:** swing; `Machine::getData/setData` (samples, formulas) saved under `<Data>`;
  `SwappableResource` (hazard-pointer swap of audio-thread data); song format v4;
  `scripts/sync-jucer.py`.
- **4b/4c machines:** Supersaw, Pluck, Organ, FM, PadSynth, Bytebeat, Sampler, Vocoder, Modular — all 12
  Caustic machine slots now have a Natron machine (table in `docs/spec.md`). Drums slots can load
  samples. `SampleData` imports WAV/AIFF/FLAC/Ogg and embeds samples in songs.
- **4d effects:** AutoWah, Limiter, Vinyl, Comb, Static Flanger, Cabinet, Octaver, Vibrato, Tremolo,
  AutoPan (18 in all); master 3-band parametric EQ.
- **4e automation:** pattern lanes (per pattern, its machine/mixer/effects) and song lanes (timeline,
  any machine or the master); linear interpolation; by parameter id.
- **4f live and UI:** live MIDI queue + all MIDI inputs + on-screen keyboard (play the selected
  machine); sample import and formula editing panels; automation lane editors; swing control.

Deferred: recording live input into patterns; parameter smoothing within notes for every machine;
undo/redo; copy/paste of patterns and blocks; MIDI learn for knobs.

## Phase 5 — Android polish ✅ (pending on-device checks)

- **5a performance:** `dsp/FastSine.h` (table sine) for drawbars, FM operators, sine oscillators and
  LFOs; per-block increments in Organ; AcidBass only calls `pow` when its pitch moves. Hidden
  benchmarks (`NatronTests "[.benchmark]"`): each machine at full polyphony costs 0.5–2% of a
  desktop core; a worst-case rack (14 machines, 28 effects, sends) ~14%. A mid-range phone core is
  roughly 3–5× slower, so a typical song (4–8 machines) should fit comfortably.
- **5b touch UI:** one colour per machine type (`Theme.h`) on rack rows, pattern pads, song blocks,
  notes and mixer strips; bigger knob cells on phones; `TouchKnob` — hold still for a moment, then
  drag, for 10× finer adjustment; narrow screens fold Save/Load/Export into a File menu.
- **5c Android:** Settings tab (output device, buffer size, live latency and CPU readout; new/demo
  song), saved between launches; Oboe (AAudio) is JUCE's first device type on Android; immersive
  full screen; the back button returns to the Rack tab before leaving the app; the song is
  autosaved (`core/Autosave`) when the app is backgrounded or closed and restored on launch.

Still to do on real hardware (needs an APK build on a device): measure output latency per buffer
size, profile on a mid-range phone, and check touch sizes by hand.

Deferred: periodic autosave while in the foreground; per-machine colour choice; tablet portrait
layout.

## Phase 6 — editing workflow ✅

From what the spec still listed and earlier phases deferred:

- **6a undo/redo:** `Session` snapshots the song before every edit (all but pattern selection,
  play/stop and the pattern/song mode). Undo applies only the differences, so playback goes on;
  machines added at the end are removed / re-added in place, and only undoing a removal from the
  middle reloads the rack. Editor presses are gestures (one step per drag); repeated edits of one
  value under a second apart (a knob being turned) merge. 100 steps; loading a song clears them.
- **6b pattern tools:** copy / paste / clear (rack pad long-press or right-click, or the pattern
  editor's Pattern... menu; pasting onto another machine type takes the notes only), transpose ±1
  / ±12, double the length. Song blocks: Duplicate / Delete menu.
- **6c recording:** with Rec on, notes played live are written into the selected machine's
  playing pattern (pattern mode, while playing), quantised to 1/16 by default; a take is one undo
  step. The engine reports notes through a lock-free queue; `LiveRecorder` pairs them.
- **6d metronome and tap tempo:** a click per beat after the master section (app setting, not in
  the song); tap tempo averages the last four gaps.
- **6e MIDI export:** song timeline or pattern loop, one track per machine (`SongExport::writeMidi`).

Deferred: count-in before recording; swing in MIDI export; MIDI learn for knobs; selecting several
notes for copy/move in the piano roll; a list of undo steps.

