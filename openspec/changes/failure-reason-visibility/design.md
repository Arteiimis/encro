## Context

`src/utils` `exec2` runs every ffmpeg/ffprobe child. Two capture modes are in use: encode, probe-segment, and compression children run with stream merging enabled — the merged capture already contains ffmpeg's stderr, but the callers discard it on failure; the ffprobe and quality-scoring paths run with merging disabled, and there the spec-pinned behavior discards stderr outright. Either way the informative `Impossible to open …` / `Unable to choose an output format …` lines never reach any record. Callers (`video_encode_runner.cpp`, `picture_compress.cpp`) log only "encoded failed" style warnings. `crash_runtime.cpp`'s `writeCrashMessage` is a short-circuit chain (file tier → logger tier → stderr), so the stderr tier is dead in practice. The three verified failures (relative-output concat, `.jpg.partial` muxer inference, preview empty-parent crash) are direct consequences; see proposal.md - Why.

## Goals / Non-Goals

**Goals:**

- The user never sees a failed-file list entry without a cause.
- A crash is visible on the terminal within one line.
- The three regressions are fixed at the root, with tests that fail without the fix.

**Non-Goals:**

- No stacktrace quality work (symbols, test context) — owned by the in-progress `hardening-crash-diagnostics` change; this change only alters destination policy in `writeCrashMessage`.
- No streaming of child stderr live to the console (reasons surface in summaries and logs; live tailing belongs to verbose echo if ever wanted).
- No exit-code taxonomy beyond the existing `exit code N` fallback.

## Decisions

- **D1 — `ExecResult` gains a `stderrText` field.** With merging off, `exec2` reads a second pipe into it under the same deadlock-safe completion the stdout pipe uses; with merging on the field stays empty (merged output already carries it). Nothing is forwarded to encro's own stderr — capture is for the caller, not the terminal.
- **D2 — reason extraction is one helper with two sources.** Defined once next to `ExecResult`: read the separate `stderrText` first (merging-off children); for merging-on children, the callers retain the merged capture on failure and the helper scans it (the surfaced result itself is capped) — first line the existing ffmpeg-error-line classifier accepts, else the last non-empty line. Empty on both → `exit code N`. Result trimmed, capped (~200 chars). Alternative rejected: switching encode/compress to merging-off — the callback overload they use has no merging-off form, and it feeds live stderr lines to the transient-status display that would be lost.
- **D3 — crash stderr tier runs whenever the log-file tier succeeds.** `writeCrashMessage` writes the log-file tier and then always prints the one-line reason plus the log path to stderr; when the log-file tier fails, the existing logger→stderr full-report fallback chain applies unchanged (spec: error-visibility MODIFIED requirement). The stacktrace goes only to the file on the healthy path. Boundary note: the in-flight `hardening-crash-diagnostics` change touches handler installation (VEH), the crash-context provider, and report assembly (`writeCrashReport`, the caller of `writeCrashMessage` whose destination policy this change alters) — the overlap is a few lines and whichever lands second rebases trivially.
- **D4 — manifest entries are bare segment names.** `list.txt` lives in the segment directory, so `file 'seg_0.ts'` resolves correctly per the concat demuxer's rules for both relative and absolute runs. Alternative rejected: absolute paths — leaks machine paths into resume state and longer manifests for no benefit.
- **D5 — compression temp keeps the target extension.** `<final-stem>.partial.jpg` (temp) → `<final-stem>.jpg` (rename) preserves both ffmpeg's muxer inference and the atomic-output contract. Alternative rejected: passing `-f image2` explicitly — couples the flag to every future format change.
- **D6 — preview parent guard.** Skip `create_directories` when `parent_path()` is empty; the path then naturally resolves against the working directory. Two lines, no new path-normalization layer.

## Risks / Trade-offs

- [Capturing stderr doubles pipe traffic on noisy children] → ffmpeg runs with `-loglevel error`; the capture is bounded by the same completion path as stdout; no memory concern beyond the existing stdout capture.
- [Reason line may be cryptic (`[in#0 @ 0x…] Impossible to open …`)] → Still strictly better than silence; the full stderr stays in the log record for depth.
- [Two changes touch `crash_runtime.cpp`] → Boundary in D3; whichever lands second rebases on a few lines.
- [Summary reason text length on narrow terminals] → Cap applies (D2); the decluttered one-line-per-file list wraps naturally.

## Migration Plan

Single build. Manifests and temp compression paths are per-run working artifacts (under the hidden `.encro` directory), so no persisted state carries the old formats; a resume across the upgrade re-derives them from the job state. The exact resume interplay is pinned by the regression tests in task 4.1 rather than asserted here. Rollback is reverting the commit.
