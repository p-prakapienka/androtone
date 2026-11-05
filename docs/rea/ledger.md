# Finding ledger

Per REA: each conclusion is tied to its evidence and confidence, and unknowns stay explicit.
"Documented" = paraphrased from the public Live 12 manual (behaviour only). "Design" = our own
choice where the manual is silent, conflicting, or the behaviour doesn't fit a touch-first MIDI
app. "Unknown" = not settled by the pages read; resolved here by a design choice.

## Documented behaviour we rely on

| Topic              | Finding                                                                                          | Source           |
| ------------------ | ------------------------------------------------------------------------------------------------ | ---------------- |
| Follow actions     | Act within a group of consecutive clips; empty slots split groups; Next wraps last→first         | launching-clips  |
| Follow actions     | Fire at their exact time when clip quantization is global/none (bypass global quantization)      | launching-clips  |
| Launch modes       | Trigger, Gate (release stops), Toggle (second press stops), Repeat (retrigger at quantization)   | launching-clips  |
| Session vs arrange | A track plays either its session clip or its arrangement, never both                             | §7.5             |
| Session vs arrange | Arrangement does not resume by itself; Back to Arrangement lights while output differs           | §7.5             |
| Arrangement record | Logs clip launches at the tracks and song positions where they happened                          | §7.5, §19.3.1    |
| New MIDI clip      | Creating a clip in an empty session slot gives a one-bar looping clip                            | §19.3.3          |
| Clip colour        | New clip takes the track colour                                                                  | §8.1.1.3         |
| Loop length        | Defaults to the clip's full length                                                               | §8.2.1           |
| Run into loop      | Playback begins at the start marker even if the loop starts later                                | §8.9             |
| Grid snapping      | Dragged notes keep their offset from the grid (groove preserved)                                 | §10.2.1          |
| Overlap            | A note placed over another's start replaces it; over its end shortens it                         | §10.5.4          |
| Velocity           | Shown by note colour saturation; a velocity lane is visible by default                           | §10.1, §10.5.12  |
| Quantize           | Has an amount %, moves notes part of the way to the grid                                         | §10.5.11         |
| Duplicate loop     | Doubles the loop and copies its notes; later notes shift to keep their place                     | §8.9             |
| Mute / solo / arm  | Solo exclusive by default; arm exclusive by default; returns can stay audible while soloing      | §18.1, §18.6     |
| Sends              | Pre taps before pan/volume/mute, post after                                                      | §18.4            |
| Automation         | Manual change of an automated control overrides it until re-enabled                              | §25.4            |
| Metronome          | Ticks once play or a clip launch starts the transport                                            | §19.4            |
| Count-in           | Delays recording; position shows negative bars until 1.1.1                                       | §19.6            |
| Stop               | Stopping twice returns the position to 1.1.1                                                     | §7.1             |
| Tap tempo          | Each tap updates tempo; more taps are more accurate                                              | §9.1.2           |
| Groove             | Timing and velocity amounts are independent; notes on the base grid don't move                  | §14.1.1          |
| Sets / undo        | A set saves clips, positions, settings and control values; undo history is not saved             | §3.21, §5.4.2    |
| MIDI export        | A clip can be exported as a standard MIDI file                                                   | §5.2             |

## Design choices (manual silent, conflicting, or out of scope)

| Topic              | Choice                                                                                           | Why                                       |
| ------------------ | ------------------------------------------------------------------------------------------------ | ----------------------------------------- |
| Launch "None"      | Applies at the start of the next audio block                                                     | block-based engine                        |
| Follow "None"      | Clip keeps playing; each launch fires its follow action once                                     | manual unclear on repeat                  |
| Previous at top    | Wraps to the bottom of the group (mirror of Next)                                                | manual silent                             |
| Tempo range        | 20–999 BPM                                                                                       | manual pages silent                       |
| Tap tempo          | Average of the last 4 intervals; a gap > 2 s starts a new sequence                               | averaging unknown                         |
| Groove             | One global swing (base 1/16) with timing % and velocity %, not groove files                      | groove files out of scope                 |
| Default grid       | 1/16, snapping on                                                                                | manual pages silent                       |
| Default note       | Drawn notes are one grid cell long, velocity 100                                                 | manual pages silent                       |
| Undo scope         | Clip, note, clip-property, arrangement and track-structure edits; mixer moves are not undoable   | manual silent on mixer moves              |
| Undo granularity   | One step per completed gesture (a whole drag is one step)                                        | manual silent                             |
| Post send + mute   | A muted track's post-fader sends are silent; pre sends keep sounding                             | inferred from pre/post definition         |
| Back to Arrangement| Resumes at the current song position (mid-clip, chasing notes)                                   | manual silent                             |
| Count-in           | Applies to session recording too                                                                 | manual silent                             |
| Plugin transport   | When the host is playing, its tempo and position drive Androtone; otherwise the internal clock  | not a manual topic                        |
| Editing gestures   | Touch: tap empty = add, tap note = select, double-tap note = delete, drag = move, drag edge = resize | keyboard/mouse modifiers out of scope |
