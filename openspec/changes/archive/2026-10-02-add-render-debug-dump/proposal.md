## Why

Styling defects reach the user before any check can see them: the three-color `2/2` ratio token and four-color summary lines (2026-09-20 session) and an stderr warning rendering mid-summary (2026-09-19 session) were all found by eyeball. Bars, colors and cursor control only render on a TTY, so under pipes — every test run and every agent session — the render layer is skipped entirely and layout, styling and stdout/stderr interleaving are invisible to all automated checks. Every styling iteration round-trips through the user.

## What Changes

- New debug environment variable `ENCRO_DEBUG_DUMP_RENDER=<file>`: when set to a non-empty path, the terminal layer appends every console write to that file, so a non-TTY run still leaves inspectable rendered output behind.
- Each dump record is the write's stream marker (`[out]`/`[err]`) plus the exact bytes `terminal::write` produced (including ANSI styling when colors are enabled) — the dump records what was rendered, it never changes what is rendered.
- Progress bars, which cannot be redirected (the bar library writes cursor escapes straight to `std::cout` and non-TTY renders are gated off), contribute a final-state frame: `ProgressContext::eraseBars` appends one plain-text line per bar (fill core, percentage or indeterminate marker, and the bar's stored postfix text — the one label slot that `addBar` seeds with the prompt and `setPostfixText` later overwrites) to the dump before clearing, on TTY and non-TTY alike.
- Stdout and stderr writes append to one file in write order, so cross-stream interleaving is observable.

## Capabilities

### New Capabilities

- `render-debug-dump`: opt-in, environment-activated dump of exactly-rendered console output (raw bytes, both streams, in order) plus progress bars' final-state frames, for layout/styling/interleaving inspection on non-TTY runs.

### Modified Capabilities

(none — `terminal-color-palette` and `console-output-conventions` requirements are untouched: the dump adds an observation path, not a rendering change; `progress-bar-lifecycle` is untouched because the frame append does not draw on the console.)

## Impact

- `src/infra/terminal.{h,cpp}`: dump hook inside `terminal::write`; env parsing via existing `processenv::readNonEmptyEnvVar`; per-write append open (debug-only path, no cross-process locking).
- `src/core/progress.{h,cpp}`: final-state frame append in `ProgressContext::eraseBars`, built from already-stored state (`postfixText()` — the seeded-or-overwritten label slot, `progressValue()`, indeterminate flag) — no second renderer of live animation.
- Tests: unit cases for the write hook (dual-stream order, stream markers, raw-byte fidelity with `--color always`) and the bar frame; no e2e needed.
- No CLI flags, no config keys, no changes to rendering behavior, colors, quiet mode, or the progress bar lifecycle.
