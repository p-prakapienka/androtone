# Reference spec: Caustic 3

Natron is a clean-room recreation of **Caustic 3** (Single Cell Software), the rack-based
music-making app for Android. This file is the feature inventory we build against. It is the
canonical answer to "how should this behave?" — when unsure, check Caustic and update this file.

## Ground rules

- **Clean room.** Work only from observed behaviour, the public user manual, and tutorials. Never
  decompile, disassemble, or copy assets (samples, presets, graphics) from the Caustic APK.
- **Own names and assets.** Machine names below are Caustic's and are used here only as
  reference labels; Natron ships its own names, artwork, samples, and presets.
- **Unverified items are marked `(verify)`.** This inventory was written from memory of the public
  manual. Before building a feature, check it against the running app and fix this file first.

## 1. Rack

- A song is a rack of up to **14 machines**. Each machine is an instrument with its own pattern
  sequencer, mixer channel, and effect slots.
- Machines are added, removed, renamed, and reordered; each has a preset browser (load/save).

### Machine types

| Caustic machine | What it is | Key controls |
| --------------- | ---------- | ------------ |
| SubSynth   | 2-osc subtractive synth | osc waveforms, mix, detune, LFO, filter (cutoff/res/type), filter + amp ADSR |
| PCMSynth   | multi-sample player | up to many samples mapped by key range, loop points, filter, ADSR, LFO |
| BassLine   | TB-303 style mono bass | saw/square, tune, cutoff, res, env mod, decay, accent, slide; own step editor with accent/slide |
| BeatBox    | 8-slot drum sampler | per-slot sample, tune, decay, pan, volume, mute; step grid per slot |
| PadSynth   | PADsynth-algorithm pad synth | harmonic table editor, bandwidth, two layers, ADSR (verify) |
| 8BitSynth  | bytebeat/expression synth | expression formula input, presets (verify) |
| Modular    | semi-modular patch synth | modules in bays, patch cables (verify) |
| Organ      | tonewheel organ | drawbars, percussion, rotary/leslie (verify) |
| Vocoder    | vocoder fed by a sample | carrier/modulator, bands (verify) |
| FMSynth    | FM synth | operators, algorithms, envelopes (verify) |
| KSSynth    | Karplus-Strong plucked string | excitation, damping, decay (verify) |
| SawSynth   | supersaw-style synth | voices, detune, filter, ADSR (verify) |

### Natron's machines

| Natron | Caustic slot | Notes |
| ------ | ------------ | ----- |
| `Subtractive` | SubSynth | 2 PolyBLEP oscs (saw/square/tri/sine), osc 2 semi + fine, LP/HP/BP state-variable filter with ADSR, amp ADSR, LFO → cutoff/pitch, 8 voices |
| `Sampler` | PCMSynth | multi-sample zones (root, key range, loop), imported WAV/AIFF/FLAC/Ogg or the built-in keys; filter, ADSR |
| `Acid` | BassLine | mono, saw/square, 4-pole ladder, env mod, decay. Pattern conventions: velocity ≥ 0.9 = **accent**; a note that overlaps the next = **slide** (verify against Caustic's own step editor, which has explicit accent/slide toggles) |
| `Drums` | BeatBox | 8 slots on notes 36–43; per slot tune/decay/pan/level/mute; kit synthesised in code (`dsp/DrumKit.cpp`), any slot can load a sample |
| `PadSynth` | PadSynth | PADsynth wavetable from 8 harmonic levels + bandwidth (rebuilt on change), ADSR |
| `Bytebeat` | 8BitSynth | C-style integer formula of `t` (compiled, fixed-stack evaluation); rate transposed by the note |
| `Modular` | Modular | fixed modules (2 osc, noise, filter, 2 env, 2 LFO) + six patch cables (source → destination × amount) instead of free cabling (verify how close this needs to be) |
| `Organ` | Organ | 9 drawbars, percussion 2nd/3rd fast/slow, click, vibrato, rotary with spin-up |
| `Vocoder` | Vocoder | 16-band channel vocoder; carrier from the notes (saw + noise); voice = built-in vowels or an imported sample |
| `FM` | FMSynth | 4 operators, 8 algorithms, op 4 feedback (Caustic's operator count and algorithms: verify) |
| `Pluck` | KSSynth | Karplus-Strong, decay calibrated to −60 dB, damping, brightness |
| `Supersaw` | SawSynth | up to 9 detuned saws spread in stereo, filter + envelope |

## 2. Pattern sequencer (per machine)

- **4 banks (A–D) × 16 patterns** per machine = 64 patterns.
- Pattern length 1, 2, 4, or 8 bars; selectable step resolution (verify: 1/16 default, 1/32, triplets).
- Piano-roll editor: add/delete/move/resize notes, velocity, zoom, scroll, copy/paste, transpose.
- BassLine / BeatBox use step-grid editors (accent, slide for BassLine).
- Per-pattern automation of machine parameters (draw curves on a pattern).
- Live keyboard for auditioning and recording into a pattern.

Natron (Phase 3): the pattern editor uses a 1/16 grid; long-press cycles velocity default (0.8)
→ accent (1.0) → soft (0.5). Pattern switching in pattern mode waits for the end of the current
pattern (verify).

## 3. Song sequencer

- Timeline with one row per machine; each cell places a pattern (e.g. `A01`) on a bar.
- Pattern blocks can be lengthened, copied, deleted; loop region; song position marker.
- Two playback modes: **pattern mode** (loop current patterns) and **song mode** (play timeline).
- Natron: a block shorter than its pattern plays the pattern's start; longer, the pattern loops from
  the block's start. Placing a block over others trims or splits them (verify against Caustic).
- Song-level automation of mixer/effect parameters (verify).

## 4. Mixer

- One channel per machine: volume, pan, 3-band EQ (low/mid/high), stereo width, **delay send**,
  **reverb send**, mute, solo.
- Master section: master volume, master delay, master reverb, master parametric EQ, limiter.

## 5. Effects

- **2 insert effect slots per machine**. Effect types (verify full list):
  Distortion, Compressor, Bitcrusher, Flanger, Phaser, Chorus, AutoWah, Parametric EQ, Limiter,
  Vinyl simulator, Comb filter, Cabinet simulator, Static flanger, Filter, Octaver, Vibrato,
  Tremolo, AutoPan. Natron implements all 18 (`effects/Effects.h`, `effects/MoreEffects.h`).
- Master delay: time synced to tempo, feedback, damping, stereo/ping-pong mode.
- Master reverb: size, damping, width, pre-delay (verify).

## 6. Transport and global

- Play/stop, tempo (BPM), swing/shuffle, metronome (verify), time signature 4/4 (verify).
- Tap tempo (verify).

## 7. Files and I/O

- Save/load songs (Caustic uses its own `.caustic` format; we use our own format, not theirs).
- Export song to WAV (and OGG/MIDI, verify); export loops/patterns.
- Import WAV samples into PCMSynth/BeatBox from device storage.
- MIDI input from USB/Bluetooth controllers (verify).

## 8. UI conventions

- Landscape, single-screen-per-view navigation: Rack → Machine → Editor → Song → Mixer → Effects.
- Large knobs dragged vertically; long-press for fine control / reset (verify).
- Dark, hardware-styled panels, one colour per machine.
