## Why

A finished run leaves the terminal cluttered and uninformative: the encode, pack, picture-compress and preview phases never clear their progress bars, so every finished bar stays on screen at 100% and the phase result lines are buried underneath. Those result lines also carry neither the elapsed time of the work they report nor any outcome signal — a reader cannot tell from `Encoded 6/8 videos → out` whether the missing two failed or were skipped, and the probe plan's file-name column stretches each row to the terminal width.

## What Changes

- Every phase that renders progress bars clears them the moment its work ends — probe, encode, pack (compact and full), picture compression, video-to-WebP conversion, preview — including the cancel and failure exits, so no finished bar survives into the next phase or the run summary.
- Every phase ends with exactly one summary line carrying the phase's own elapsed time: `Probed 8/8 videos in 24s`, `Encoded 8/8 videos → D:\out in 12m:34s`, `Packed 1 archive(s) → D:\out\packed in 14s`, `Compressed 12/12 pictures in 8s`, `Converted 12/12 videos to WebP in 42s`, `Preview written to: D:\preview.mp4 in 35s`.
- Phase summary lines name their outcome classes where they occur: `(2 failed, 1 skipped)` on the encode line, `(1 not probed)` on the probe line. Media-mode packing gains a completion line of its own, and picture compression and the video-to-WebP conversion gain ones they currently lack entirely.
- With colors enabled, each token of a summary line carries the role of the outcome it reports: verb Good on full success / Bad when the phase had a failure / Warn when it only skipped work, succeeded count Good, failed count Bad, skipped count Warn, counts, paths and durations Accent. Nothing is colored when styling is disabled.
- Elapsed time renders compactly on narration lines — `24s` below a minute, `12m:34s` below an hour, `1h:05m` from an hour — while progress-bar badges keep their fixed-width `00m:24s` form for alignment.
- The probe plan table caps its file-name column so no row exceeds 85 display columns, truncating long names mid-string with the extension preserved, and separates the `Total:` line from the table body with one blank line; the table itself gains no styling.
- The encode summary block prints before the packing phase, matching execution order, so a run's last product line is its packing result.
- Scan start lines stop repeating the input root (`Scanning for videos...` for the video scan, `Scanning for files...` for the directory pack, and matching wording for the multi-file variant); the completion line remains the single line that names the root on non-TTY output.
- Out of scope, recorded in design.md: the existing requirement that a very narrow terminal falls back to a two-line plan layout is not implemented by the current table renderer; this change leaves it untouched.

## Capabilities

### New Capabilities

- `progress-bar-lifecycle`: phase-scoped progress bars — created with their phase, cleared when the phase's work ends on every exit path (success, cancel, failure), and never drawn again once cleared.

### Modified Capabilities

- `pipeline-narration`: scan start lines no longer name the input root; every bar-rendering phase ends with exactly one summary line stating its counts (for counted phases), its destination (for file-producing phases) and its elapsed time, and the duration wording convention is defined.
- `plan-output-formatting`: the name column is capped to an 85-column row budget, `Total:` is preceded by one blank line, and the post-encode summary line carries elapsed time and outcome segments.
- `video-encode-probing`: the plan block is preceded by the probe phase's summary line; the "no standalone probing-completion line" rule is replaced by that line's contract.
- `terminal-color-palette`: outcome-based role assignment for the tokens of a phase summary line.
- `video-preview`: the written-to summary line carries the run's elapsed time.

## Impact

- **Code**: `src/core/progress.{h,cpp}` (bar lifecycle seam), `src/core/display_text.h` (shared compact duration formatter), `src/infra/terminal.h` (token-level role helper), `src/video/{encode_probe,video_batch_execution,video_process}.cpp`, `src/pack/{pack,pack_service,packer}.cpp`, `src/picture/{picture_compress,picture_process,picture_video_webp}.cpp`, `src/preview/preview_process.cpp`.
- **APIs**: new/changed internal signatures only — `printProbePlan` (phase elapsed), `printEncodingSummary` (elapsed, skipped count), `EncodingBatchOutcome` (encode elapsed, skipped count), `runPackTaskPlan` (progress context parameter), preview report (elapsed). No CLI flag or config key changes.
- **Tests**: unit cases in `tests/display_text_tests.cpp` (duration formatter) and `tests/infra/progress_tests.cpp` (bar lifecycle), `tests/video/encode_probe_tests.cpp` (row width cap, probe summary line), colour-span cases in `tests/infra/terminal_tests.cpp`, packing/picture/preview summary assertions, and an e2e assertion that stdout prints the encode summary before the packing line.
- **Specs**: the six capabilities listed above.
