# Roadmap

Target: `docs/spec.md` (Caustic 3, clean room). Each step should be small, land with tests, and
keep `LyeTests` green.

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

- `lye/` app with its own desktop CMake (app + `LyeTests`) and a Projucer export for Android.
- Engine core with no GUI dependency: `Transport` (block-size-independent beat ↔ sample
  mapping, tempo re-anchoring), polyphonic `Pattern` (notes in beats, any length, 1/2/4/8 bars),
  `PatternPlayer` (sample-accurate MIDI, notes longer than a step or the loop, retrigger, off
  before on), `Machine` interface, `Engine` (rack of up to 14 machines, volume, master).
- Placeholder `TestToneMachine` and a demo song; minimal app shell (play/stop, tempo, position).
- Offline Catch2 tests (transport, player, engine) and Linux CI (`.github/workflows/lye.yml`).

## Phase 1 — core model

1. Lock-free command queue so the UI can edit the rack and patterns while audio runs (today
   edits are only safe before audio starts).
2. Parameter model: every machine/effect parameter has an id, range, default, and a smoothed
   real-time value; this is what presets, automation, and the UI bind to.
3. Pattern banks: 4 banks × 16 patterns per machine; pattern switch quantised to pattern end
   (verify against Caustic).
4. Song save/load as `ValueTree` (XML on disk), our own format.

## Phase 2 — first playable loop

5. SubSynth-equivalent (2 osc, filter, filter/amp ADSR, LFO).
6. BeatBox-equivalent (8 sample slots, step grid; our own royalty-free kit).
7. BassLine-equivalent (mono 303-style voice, accent, slide).
8. Mixer channel: pan, 3-band EQ, width, mute/solo; master delay, reverb, limiter.
9. Golden-file render tests for these machines (tolerance-based, not bit-exact).

## Phase 3 — arrangement and editing

10. Song sequencer: pattern blocks per machine on a bar timeline; pattern vs song mode.
11. Piano-roll editor (touch: tap to add, drag to move/resize, pinch to zoom); step editors.
12. Insert effects, 2 per machine: Distortion, Bitcrusher, Chorus, Phaser, Flanger, Compressor,
    Filter, Parametric EQ first.
13. WAV export through the offline renderer.

## Phase 4 — breadth

14. Remaining machines: PCMSynth, FMSynth, KSSynth, SawSynth, Organ, PadSynth, 8BitSynth,
    Vocoder, Modular (roughly by effort).
15. Remaining effects; pattern and song automation.
16. MIDI input, sample import from device storage, swing.

## Phase 5 — Android polish

17. Touch-first layout (landscape, big knobs, long-press fine adjust), one colour per machine.
18. Latency and CPU: Oboe/AAudio settings, profiling on a mid-range device.
