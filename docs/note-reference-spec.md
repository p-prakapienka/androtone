# Reference spec: Ableton Note feature set

Androtone's product goal is an Android music sketchpad with the same workflow as Ableton Note
(iOS only). This file records what Note does, as far as public sources describe it, so that
"what should this feature do?" has one answer. `roadmap.md` maps it onto the codebase.

_Compiled 2026-10-29 from press coverage, App Store listings and search excerpts of the Note
manual. Ableton's own sites (ableton.com, help.ableton.com, the manual PDF) could not be opened
directly from the research session, so manual details are second-hand. Rows marked **verify**
should be checked against the app or the [manual](https://www.ableton.com/en/note/manual/)
before building them._

## Ground rules (clean-room)

- Build from observed behaviour and public documentation only. Never decompile or inspect the
  Note binary or its data files.
- Do not ship Ableton names, logos, icons, UI artwork, samples, kits or presets. Sound content
  must be our own or carry a licence that allows redistribution. This also applies to the
  current kick sample (`wa_free_ldrum_kick_06_t1.wav`): check its licence before shipping.
- Copy the workflow and capabilities, not the look. Androtone keeps its own visual identity
  (`AndrotoneLookAndFeel`).

Confidence key: **C** = several independent sources agree · **S** = single source ·
**verify** = conflicting or unconfirmed.

## 1. Set structure

| Feature | Note behaviour | Conf. |
| --- | --- | --- |
| Tracks | Up to 8 tracks, laid out left to right | C |
| Clip slots / scenes | 8 clip slots per track, i.e. 8 scenes | C (early reviews) — verify for 2.x |
| Clip length | Up to 16 bars (help centre); early reviews said 8 | verify |
| Clip launching | One clip per track, or a whole row as a scene | C |
| Clip and scene editing | Add, duplicate, move and delete clips and scenes | C |
| Arrangement / timeline | None; the session grid is the whole song | C |
| Set settings | Tempo, key and scale, metronome; arbitrary time signatures (2.1) | C / S |

## 2. Track types and instruments

| Feature | Note behaviour | Conf. |
| --- | --- | --- |
| Drum track | Drum Rack with a 16-pad kit; dozens of factory Drum Sampler kits (56 → 76 across versions) | C |
| Drum pad "16 Pitches" | Play one drum sound chromatically across all 16 pads | S |
| Drum Sampler per-pad FX | FX Type: Stretch, Loop, Pitch Env, Punch, 8-Bit, FM, Ring Mod, Sub Osc, Noise | S (manual excerpt) |
| Melodic track | Synth presets (Drift-based patches) and sampled instruments | C |
| Melodic Sampler | One sample repitched across the pads | C |
| Audio tracks | Record audio straight into the set; audio clip editor with warp markers (2.x) | S — verify version |
| Bounce | Copy a MIDI clip and paste it onto an audio track to render it (2.1) | S |

## 3. Playing and recording

| Feature | Note behaviour | Conf. |
| --- | --- | --- |
| Drum pads | 4×4 velocity-sensitive pads | C |
| Melodic pads | Pad grid (25 pads mentioned) or piano keys | S |
| Key / scale | Set-wide root and scale. "In Key" off by default (all 12 notes); on, pads show only in-scale notes | C |
| Pad layouts | Fourths (each row a perfect fourth above the one below) and Sequential | S |
| Repeat | Note repeat at a chosen rate, in the Pads and Single layouts | C |
| Capture | Play freely without recording, then tap Capture to turn what you just played into a clip | C |
| Recording | Record into a clip in time with the transport, with optional quantise | C |
| Timing fixes | Quantise, or nudge notes | C |
| Sampling | Record through the microphone, then cut, filter and repitch, into a drum pad or a Melodic Sampler | C |
| Controllers | External MIDI controllers, including poly aftertouch for Drift patches | S |

## 4. Editing

| Feature | Note behaviour | Conf. |
| --- | --- | --- |
| MIDI editor (1.3, March 2025) | Grid / piano-roll editing; tap to add notes; empty clips can be created and filled | C |
| Multi-note edits | Add, delete or adjust several notes at once | C |
| Triplet grid | Triplet mode in the editor | S (1.4 bug fix) |
| Copy / paste notes | Users asked for it in 2025; current status unknown | verify |
| Undo | Not confirmed by any source | verify |

## 5. Mixing and effects

| Feature | Note behaviour | Conf. |
| --- | --- | --- |
| Mixer | Per-track level fader, mute and solo | C |
| Effects (1.x) | Channel EQ, Chorus-Ensemble, Delay, Phaser-Flanger, Redux, Reverb, Saturator | S (Sound On Sound) |
| Effects (2.x) | Adds Erosion and Auto Shift, plus an updated Auto Pan | S |
| Sends | Not confirmed for Note. Androtone already has reverb and delay sends, so it keeps them | — |

## 6. Getting music out

| Feature | Note behaviour | Conf. |
| --- | --- | --- |
| Audio export | Export the set as an audio file to share | C |
| To Live | Send a set to Ableton Live through Ableton Cloud (one way: Live can open Note sets, Note cannot open Live sets) | C |
| MIDI import/export | Not available, at least in early versions | S |
| Ableton Link / Link Audio | Link Audio arrived with Live 12.4; Note support unconfirmed | verify |
| User library | Save audio recordings to the user library (2.1) | S |

## 7. Android equivalents for iOS-only parts

| Note (iOS) | Androtone (Android) |
| --- | --- |
| Ableton Cloud → Live | Android share sheet / Storage Access Framework: WAV mixdown, per-track stems and a Standard MIDI File. Exporting Live `.als` sets is an open decision (undocumented format, clean-room risk) |
| Core Audio low latency | JUCE's Oboe backend (AAudio), with a measured round-trip latency target |
| iOS microphone permission | `RECORD_AUDIO`, already requested by the Android exporter (`microphonePermissionNeeded="1"`) |
| Ableton Link | Link SDK is GPL-2.0+ or under a commercial licence from Ableton. Open decision |
| CoreMIDI controllers | JUCE `MidiInput` over USB and BLE MIDI on Android |

## Sources

- [MusicRadar: first look](https://www.musicradar.com/news/ableton-note-app-iphone-ableton-live)
- [MusicRadar: the update that "finally turned Note into the app I always wanted"](https://www.musicradar.com/music-tech/software-apps/with-its-latest-free-update-ableton-has-finally-turned-note-into-the-app-i-always-wanted-it-to-be)
- [Sound On Sound review](https://www.soundonsound.com/reviews/ableton-note)
- [CDM: Note 2.1](https://cdm.link/ableton-note-2-1/)
- [Mixdown: MIDI editor added to Note](https://mixdownmag.com.au/news/ableton-add-midi-editor-capabilities-to-ableton-note-app/)
- [App Store listing](https://apps.apple.com/us/app/ableton-note/id1633243177)
- [Resident Advisor: Ableton presents Note](https://ra.co/news/77972)
- [Ableton Note manual](https://www.ableton.com/en/note/manual/) · [Note release notes](https://www.ableton.com/en/release-notes/note/) · [Working with samples in Note](https://help.ableton.com/hc/en-us/articles/5573865783452-Working-with-Samples-in-Note) (not opened directly; cited through search excerpts)
