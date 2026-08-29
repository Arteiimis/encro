## Context

Single-input preview already drives one progress bar through its pipeline (`runSingleInput` in `src/preview/preview_process.cpp`: `ProgressContext` + `CursorGuard`, one bar, `BarSlot` carries context/handle/phase-base, phases probe 0-40% / windows 40-85% / render 85-100%). Two-input preview runs entirely without a bar and prints its score list twice (once after scoring, once after the render).

The two-input pipeline is: ffprobe both inputs (fast; the encoded file's second `probeVideo` call hits `runtime.videoInfoCache`), then `scoreComparisonWindows` — a *sequential* per-window loop over `measureSegmentQuality` (the slow phase) — then one blocking `exec2` render. See proposal.md for motivation; the delta spec defines the observable behavior.

## Goals / Non-Goals

- Goals: same one-bar UX as single-input, per-window scoring progress, summary printed once after render, failure tone on render failure.
- Non-Goals: no ffmpeg `-progress` parsing during the render (single-input also just jumps 85→100 after the render); no stop-signal changes; no CLI changes; no metric-chain changes.

## Decisions

- **Mirror the single-input scaffolding, not an abstraction.** Build `ProgressContext` + `CursorGuard` + one bar directly in `run()`'s two-input branch and reuse the existing `BarSlot` struct. The two flows differ (no encode phase, sequential scoring vs parallel task batch), so a shared pipeline-runner would be an abstraction over one user each. Alternative rejected: generalize `runSingleInput`'s plumbing into a common driver.
- **Phase allocation: probe 0-10%, scoring 10-85%, render 85-100%.** Probing is two cached ffprobe calls (fast), scoring is the slow sequential phase and gets the bulk, render matches single-input's "jump to 100 on completion" behavior. Under `--start/--duration` scoring is skipped: set the bar to 85% before the render so the report stays consistent.
- **Score-window updates inside `scoreComparisonWindows`.** Add an optional `BarSlot` parameter (pointer, `nullptr` when not applicable); after each window, set progress `10 + 75 * done/total` and postfix `Scoring windows: done/total`. The loop is sequential, so increments are exact — no task-executor involvement.
- **Drop the pre-render `printWindows` call.** The post-render print (already unconditional) becomes the single summary. This also removes today's duplicate list in sampled-window runs. Strings and format unchanged.
- **Failure handling follows existing tone API.** Render failure → `setTone(Failure)` + postfix `Preview generation failed`, matching `renderAndReportSingleInput`. Per-window scoring failures keep the current behavior (score prints `-`, run succeeds) and do not touch the tone.
- **Bar label** `Previewing: <original filename>` — same pattern as single-input.

## Risks / Trade-offs

- [Render progress stays flat during a long ffmpeg exec] → Accepted; identical to single-input today. Upgrade path: parse `-progress pipe:1` in `renderPreview` later, shared by both modes.
- [e2e cannot see the bar itself (non-TTY renders nothing)] → Verify the bar phases only at the summary-timing level: a new fake-toolchain e2e asserts the score list appears exactly once (today's duplicate would fail it) and that the run succeeds; bar rendering stays covered by `progress_tests` and manual TTY checks.

## Migration Plan

Single-commit change: code + e2e test together, per repo convention. No state-file or CLI migration; rollback is `git revert`.

## Open Questions

(none)
