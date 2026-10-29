# Status

What is built, against the requirement IDs in `spec.md`. "Tested" means covered by
`RiffbookTests` (headless); everything else was checked with headless screenshots only and
still needs a pass on a real phone.

| Area | IDs | State |
| --- | --- | --- |
| Set library | LIB-1..LIB-6 | Done: list, demos, new set with random drum/bass/melodic presets, preview play, duplicate/rename/delete, `.riffbook` bundle share and import, autosave (temp file + rename), save on suspend. No artwork. |
| Session view | SES-1..SES-3 | Done |
| Tracks | TRK-1..TRK-3 | Done (add by category, long-press duplicate/delete, drag to reorder). Edits tested. |
| Clips | CLIP-1..CLIP-6 | Done. Launch/stop at the next downbeat, loops, seamless duplicate: tested sample-exactly. |
| Scenes | SCN-1..SCN-3 | Done. Launch semantics tested. |
| Mixer | MIX-1..MIX-2 | Done, with meters. Master peak limiter (-1 dBFS) added beyond the spec. |
| Set settings | SET-1..SET-4 | Done (slider, ±1, tap tempo, metronome, In Key/root/scale with preview pads, export). |
| Instrument view | INS-1..INS-5 | Done |
| Browser | DEV-3, DEV-4, DEV-23 | Done: categories, search, type filter, favorites, preview on tap with try-out pads, Done/Cancel as one undo step, user presets (save/delete). |
| Devices | DEV-1..DEV-23 | Done: Analog (42 params), Wave (43 params, 8 wavetables), Drum Kit (16 Pad Samplers with all 9 playback FX), Sampler; 9 effects; ~70 track presets, ~50 effect presets, 74 synthesised factory sounds. All tested. |
| Automation | DEV-30..DEV-32 | Done: record while playing, Add commits, insert/delete, playback tested. |
| Pads | PAD-1..PAD-8 | Done (drum 4x4, single, 5x5 Fourths/Sequential, keys, In Key, octave, repeat with transport-locked grid). Repeat tested. |
| Capture | CAP-1..CAP-7 | Done: free capture with tempo detection, create clip while running, fixed length, overdub Add, clear, timeout. Tested. |
| MIDI editor | EDIT-1..EDIT-12 | Done: preview (pending notes, automation lines), full-screen editor with draw/select/move/resize/velocity gestures, loop brace, note-edit menu (Operations, Transpose, Quantization, Velocity, Pitches), remembered state. Long-press on an empty preview creates a clip; automation draws in preview and full-screen grid. Two-finger gestures untested on a device. |
| Samples | SMP-1..SMP-8 | Done: sample browser (factory, recordings, this set), waveform with start/gain drags and pinch zoom, mic and resample recording with trim/normalise, record-into-next-pad, choke/mode/save/swap options, input monitoring. Recording paths tested headless; mic needs a device test. |
| MIDI input | MIDI-1..MIDI-3 | Done (USB/BLE devices, active track, learn mappings, pitch bend ±2 st, poly/channel pressure to filter). Bend and pressure measured in rendered audio; not tested with hardware. |
| Export | EXP-1..EXP-3 | Done (WAV 16/24, FLAC; progress, preview, share). Render tested. |
| App settings | APP-1..APP-8 | Done |
| Global | GLB-1..GLB-3 | Undo/redo tested; Oboe low-latency output via JUCE; mic permission requested on first record. |

## Known gaps

- Ableton Link, Cloud, `.ablbundle`: out of scope (see spec).
- Not yet run on a physical Android device: latency, touch feel, two-finger gestures, microphone
  and Bluetooth MIDI, and CPU load on phones need a hands-on pass.

## Beyond the spec

- Master peak limiter (-1 dBFS).
- Factory sounds render on a background thread at startup, so opening a set never stalls.
- `Tests/RealtimeTests.cpp` fails on any heap allocation or free in the audio callback.
