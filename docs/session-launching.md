# Session launching

Behaviour of the Session tab's clip launcher. The feature set follows the clip-launching workflow
described in public DAW documentation (launch quantization, launch modes, follow actions); the
implementation, naming and UI are Cliptone's own.

## Timing model

- One **global step grid** of 16th notes, counted from transport start (`StepSequencer`).
- **Launch quantization** (control panel): None, 1/16, 1/8, 1/4, 1/2, 1, 2, 4 or 8 bars. A queued
  clip / scene / stop takes effect on the next grid step that is a multiple of that size. "None"
  applies it at the start of the next audio block, and the clip then runs on its own phase.
- Notes may be longer than one step; noteOffs are scheduled per track in beats, so they follow
  tempo changes. A retriggered pitch that is still sounding is released first.

## Launching

- Tapping a clip queues it (it blinks until it starts). Tapping an empty slot (`+`) queues a stop.
- Launching a clip or scene while the transport is stopped starts the transport.
- **Scene** (`▶`): launches every track's clip in that row; tracks whose slot is empty stop.
- **Stop** row: per-track stop and stop-all, both quantized.
- Transport stop silences everything and rewinds; the clips that were playing start again from
  the top on the next play.

## Launch modes (per clip)

| Mode    | Press           | Release                                   |
| ------- | --------------- | ----------------------------------------- |
| Trigger | launch          | —                                         |
| Gate    | launch          | stop the track                            |
| Toggle  | launch, or stop if it is already playing / queued | —       |
| Repeat  | launch, then relaunch on every quantization boundary while held | stop repeating |

## Follow actions (per clip, off by default)

- Two actions, A and B, chosen by relative chance (weights 0–100 each; both 0 = nothing happens).
- Fire after the clip has played through *N* passes (**linked**), or after a fixed time in
  bars.beats.16ths (**unlinked**), counted from launch. Each launch fires its follow action once.
- They fire exactly on time, bypassing launch quantization.
- Actions work within the clip's **group**: the run of non-empty slots around it on its track.
  `None`, `Stop`, `Again`, `Previous`/`Next` (wrapping within the group), `First`, `Last`,
  `Any` (may repeat the current clip), `Other` (never repeats unless it is alone).

## Editing

Turn on `✎` (top-left of the Session tab) and tap a clip, or right-click a clip on desktop, to
open its launch-mode / follow-action panel. Clip labels show `G`/`T`/`R` for non-Trigger modes and
`↷` when follow actions are on.

## Song mode

With the loop button off, the next scene launches automatically (unquantized) when the longest
playing clip completes a pass.

## Not implemented yet

Per-clip launch quantization, legato launching, the "jump to slot" follow action, scene follow
actions, and a global follow-action kill switch.
