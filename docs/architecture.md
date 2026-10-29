# Architecture

## Build layout

- All app code lives in two JUCE modules under `modules/`:
  - `rb_engine` — model, sequencer, devices (instruments/effects), capture, offline render.
    No GUI or device dependencies, so it is unit-tested headless.
  - `rb_app` — audio device glue (`AudioEngine`), set library, the touch UI.
- `Source/Main.cpp` — the JUCEApplication.
- `Tests/` — `RiffbookTests` console app (JUCE `UnitTest`s). Every `Tests/*.cpp` is picked up.
- `CMakeLists.txt` — desktop app + tests. JUCE comes from `-DRB_JUCE_DIR=<checkout>` or is
  fetched with git.
- `Riffbook.jucer` — the Android build. CI runs `Projucer --resave Riffbook.jucer` to generate
  `Builds/Android` (ignored by git) and then `gradlew assembleDebug`. Modules are referenced by
  path, so adding source files never requires editing the `.jucer` or CMake.

Adding a source file: create it in the module, then add an `#include` for it in the module's
amalgamation file (`rb_engine.cpp`, `rb_engine_instruments.cpp`, `rb_engine_effects.cpp`,
`rb_engine_presets.cpp`, `rb_app.cpp`). Public headers also go into `rb_engine.h` / `rb_app.h`.

## Threads and data flow

```
 UI (message thread)                         audio thread
 ───────────────────                         ────────────
 Document ── edit() ──> Set (model)
     │ publish(): builds EngineSnapshot ───> SessionProcessor::process()
     │   (immutable; processors prepared)       reads snapshot, renders tracks
 UI ── post(Command) ──── lock-free FIFO ──>   launches clips, live notes
 CaptureBuffer <── readCaptureEvents() ─── FIFO <── every live note (after Repeat)
 UI timers <── TrackState atomics ─────────────── playing slot, clip position, levels
```

- **Set** (`model/Set.h`) is plain data. Clips are `shared_ptr<const Clip>`: an edit builds a
  new clip, so undo snapshots (whole `Set` copies) share unchanged clips.
- **Document** owns the open set: undo/redo stack, gestures (one undo step per knob drag),
  autosave (`set.json`, written via temp file + rename), and snapshot publishing.
- **EngineSnapshot** holds everything the audio thread needs, including the processors. The
  audio thread only ever holds raw pointers; snapshots are retired on the message thread once the
  audio thread has acknowledged a newer one (`SessionProcessor::collectGarbage`).
- **Processors** (instruments, effects) are created and prepared on the message thread and reused
  across snapshots while their type is unchanged, so editing never interrupts sound.
- **SessionProcessor** keeps transport time in beats: `clockBeat` always runs (repeat with the
  transport stopped), `songBeat` runs while playing. Launches are quantised to the next bar.
  Events exactly on a block boundary always land at offset 0 of the later block.

## Devices

Interfaces in `devices/Device.h`:

- `InstrumentProcessor`: `prepare`, `setParameters` (every block, values in real units in spec
  order), `setPadParameters`/`setSample` (Drum Kit, Sampler), `process(dry, send, events)`.
  The Drum Kit writes each pad's send amount into `send`; the track's first effect slot then
  acts as a send effect (Pad FX) and the second as an insert (Kit FX).
- `EffectProcessor`: in-place stereo processing, `setTempo` for synced effects.

Each device declares its parameters with a `DeviceSpec` (`model/Params.h`). Values are stored by
parameter id in sets and presets, so specs can grow without breaking saved sets. Automation
lanes target `(device, pad, paramIndex)`.

Factory sounds (`devices/FactorySounds.*`) are synthesised in code — no third-party samples.
Presets (`devices/presets/*.cpp`) are data in code; user presets are JSON files.

## Testing

`ProbeInstrument` (Tests/TestHelpers.h) records note events with absolute sample positions, so
sequencer behaviour (launch quantisation, loops, repeat, scenes) is checked sample-exactly
against the spec. `Rig` renders offline at 48 kHz / 120 BPM where one beat is 24000 samples.

### Real-time safety

`Tests/RealtimeTests.cpp` replaces the global `operator new`/`delete` in the test binary and
fails if any heap allocation or free happens inside `SessionProcessor::process` while it plays
every demo set, every factory preset (live notes, chords, repeat), every effect preset, random
edits while playing (parameters, presets, effects, samples, mixer, clip edits, track moves,
duplicates and deletes), recording, and rapid snapshot publishing. Any new code on the audio
path is covered by adding a scenario there. Avoid `std::stable_sort` (allocates), containers
that grow, `juce::String`, and anything that can lock or log.
