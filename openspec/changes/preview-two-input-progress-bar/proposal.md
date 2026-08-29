## Why

Two-input (`original + encoded`) preview runs probing, per-window scoring, and the render with no progress output at all; window scoring alone (VMAF over up to 5 windows) can take minutes of total silence, which reads as a hang. Single-input preview already shows one progress bar spanning its pipeline, so the two modes behave inconsistently.

## What Changes

- Add a single progress bar to two-input preview spanning its pipeline: probe both inputs (0-10%), score windows (10-85%, skipped under `--start/--duration`), render comparison (85-100%).
- Move the window-score list and written-to line to print only after the render completes (matching the single-input reporting rule and removing the current duplicate pre-render score list).
- Bar follows the same rendering rules as the single-input bar: same style as encode bars, cursor hidden while active, non-TTY output renders nothing; failure sets a failure tone with a postfix label.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `video-preview`: two-input mode SHALL show one progress bar spanning probe, window scoring, and render, and SHALL print the summary only after the render completes.

## Impact

- `src/preview/preview_process.cpp` — two-input branch of `run()`: create a `progress::ProgressContext` + `CursorGuard`, thread a bar slot into `scoreComparisonWindows` and the render/report path, drop the pre-render `printWindows` call.
- No CLI, state-file, or external interface changes.
- Tests: e2e two-input preview (summary-after-render ordering); no new unit surface (bar rendering itself is not asserted, same as single-input).
