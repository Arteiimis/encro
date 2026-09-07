## Why

Every failure path currently hides the cause from the user. Three verified failures on the dev machine (RTX 3070, real ffmpeg): a video encode with a relative `-o out` fails 100% of files because the concat manifest stores run-relative paths that the ffmpeg concat demuxer re-resolves against the manifest's own directory (`Impossible to open 'out/…/out/…/seg_0.ts'`); picture compression with `-c` fails 100% because outputs are written as `<name>.jpg.partial`, an extension ffmpeg cannot map to a muxer; `preview --output bare-name.mp4` crashes on `create_directories("")` from an empty parent path. In all three cases the real ffmpeg error appears nowhere — not on the console (the summary lists bare file paths) and not in the log: the encode and compress children run with merged output capture (which contains ffmpeg's stderr), and the callers simply discard that capture on failure; the subprocess utility additionally discards child stderr outright when stream merging is off (the ffprobe and quality-scoring paths), a behavior pinned by the `subprocess-exec` spec. Crashes are equally invisible: the crash handler's stderr tier only fires when both log tiers fail, so a crash leaves the terminal silent with exit code 1. For a batch tool whose selling point is resumable unattended runs, "2 failed" with no reason is the worst possible failure mode.

## What Changes

- `exec2` with stream merging disabled (the ffprobe and quality-scoring paths) captures the child's stderr separately into the result instead of discarding it; it is still never forwarded to encro's own stderr. Encode and compress children keep their merged capture, and their callers retain a bounded tail of it on failure instead of discarding it.
- Subprocess-backed task failures (video encode, probe, picture compression) record a short failure reason — the child's first meaningful diagnostic line, read from the separately captured stderr when merging is off or from the retained merged-output tail when merging is on, or an `exit code N` fallback when neither carries anything — and every console failed-task list shows it next to the file (`a_test.mp4: Impossible to open '...'`). On non-zero exits the reason is logged at warning level.
- Crash handling stops short-circuiting: the one-line crash reason always prints to stderr (with the log-file path so the stacktrace can be found) in addition to the existing log-file tier. The in-progress `hardening-crash-diagnostics` change owns stacktrace symbol quality; this change only fixes the destination policy.
- Fix the relative-output concat failure: concat manifests reference segment files by paths resolvable from the manifest's own directory, so assembly works with relative and absolute output directories alike.
- Fix the picture compression failure: compression temp outputs keep a recognizable media extension (renamed atomically as before), so the encoder infers the container.
- Fix the preview crash: an explicit `--output` with no directory component resolves against the working directory (directory creation tolerates an empty parent), instead of crashing.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `subprocess-exec`: the output-capture requirement changes from "stderr is discarded when merging is disabled" to "stderr is captured separately into the result".
- `error-visibility`: a new requirement for task failure reasons (recorded from the child's diagnostic output, surfaced in console failed-task lists); the durable-crash-report requirement gains the terminal-visibility policy (one-line reason plus log path on stderr whenever the log-file tier succeeds; full report on stderr only as the last-resort tier).
- `video-frame-resume`: the segmented-encoding requirement gains the manifest path-resolution contract that fixes relative output directories.
- `picture-compress-resume`: a new requirement keeps temp compression outputs recognizable to the encoder while preserving atomic-rename semantics.
- `video-preview`: a new requirement honors explicit output paths as given, including bare filenames.

## Impact

- `src/utils` (`exec2`/`ExecResult`): separate stderr capture when merging is off.
- `src/video/video_encode_runner.cpp`, `src/video/video_batch_execution.cpp`, `src/picture/picture_compress.cpp`, `src/picture/picture_process.cpp`, `src/pack`: record the reason, pass it into failed-task lists (slotting into the summary shape defined by `pipeline-output-declutter`; ordering between the two changes is free).
- `src/video` segment assembly: manifest entries become manifest-relative (bare segment names).
- `src/preview/preview_process.cpp`: empty-parent guard for explicit output paths.
- `src/infra/crash_runtime.cpp`: stderr tier always runs (coordinate with the in-progress `hardening-crash-diagnostics` change touching the same file).
- Tests: exec2 capture units, reason-surfacing assertions in encode/compress summaries, e2e regressions for the three fixes (fake tool covers stderr emission; `[real-ffmpeg]` tag covers the concat and muxer fixes end to end).
