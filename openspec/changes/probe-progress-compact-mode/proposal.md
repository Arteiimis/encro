## Why

`-F/--full-progress` is documented as the switch that adds per-worker bars
(`show full progress with per-worker encoding bars and per-archive packing
bars`), and the other phases honour it: encoding, packing, and picture runs all
derive `compact` from `!config.fullProgress`. The probe phase does not read the
flag at all, so it always renders the full-progress layout — one bar per worker
slot plus an Overall bar whose threshold is the full-mode one. The default
(compact) run therefore gets *more* bars during probing than during encoding,
which is the opposite of what `-F` promises. The probe spec states only the
full-progress layout and is silent on the compact one, so the rule has to be
extended there, not merely re-implemented.

## What Changes

- `runProbePhase` derives a `compact` flag from `config.fullProgress` (the same
  `!ctx.config.fullProgress` every other phase uses) and applies the encode
  phase's two decisions to its bars: in compact mode the per-worker slot bars
  are not created at all when the batch has more than one file, and the Overall
  bar appears whenever the batch has more than one file rather than only when
  the batch exceeds the worker count.
- In compact mode with more than one file the probe phase renders exactly one
  Overall bar (the per-worker layout collapses to it).
- A single-file probe is unchanged in both modes: the encode rule it mirrors
  keeps one slot bar and no Overall bar, because the slot-bar suppression only
  applies above one file and the Overall bar needs more than one file.
- `--full-progress` keeps today's probe layout unchanged.
- The bar-creation rules move to one shared place so the probe and encode
  phases cannot drift apart again.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `video-encode-probing`: the "Probing shows progress feedback" requirement
  gains the compact/full-progress split — per-worker slot bars are a
  full-progress affordance, and compact mode shows the Overall bar alone.

## Impact

- `src/video/encode_probe.cpp` — `runProbePhase` (bar creation around
  lines 842-854) and `initSlotBars`; the bar creation is extracted into a
  testable function, `runProbePhase` gains an optional injected
  `ProgressContext`, and `ProbeProgress.slotBars` becomes possibly empty because
  compact mode creates no slot bars, so the per-task bar updates need a guard.
- `src/video/video_batch_execution.h` — `EncodingProgressState`'s two private
  bar-creation rules are the behaviour being mirrored; the two booleans move to
  shared predicates both phases call (labels and roles stay where they are).
- `src/core/progress.h` — gains the two layout predicates and a `barCount()`
  accessor so a test can observe a `ProgressContext`'s layout.
- `src/video/encode_probe.h` — the `runProbePhase` comment claiming "one
  progress bar per file" is already wrong and is corrected.
- Tests: the `[encode-probe]` cases run with a non-TTY stdout, where bars render
  nothing, so they cannot observe bar counts today — hence the `barCount()`
  accessor, the extracted creation function, and the injectable context the new
  tests use.
- No CLI, config, or job-state change; `-F` keeps its meaning everywhere else.
