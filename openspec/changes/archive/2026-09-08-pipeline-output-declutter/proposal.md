## Why

A typical encode run prints ~15 stdout lines, and several of them announce mechanics rather than outcomes: a scan start line paired with a scan completion line, a "Probing complete" line immediately before a table that itself proves probing completed, a "Scheduling N video(s) with max M concurrent encode job(s)" line, and a two-line header ("All encoding tasks completed." / "Summary:") in front of a five-line summary that restates one fact five ways. Two of these lines are actively misleading: "Probing complete" prints even when probing was skipped for every file (short videos), and "All encoding tasks completed." prints even when every encode failed. The plan table also renders in full — rules, header, totals — when no file carries any measured data, showing a table of dashes with a nonsense `−100%` total. Preview and organize already show the target density (three lines, each with a job); the video, picture, and pack flows should match it.

## What Changes

- Scan narration becomes one line per phase on non-TTY output (the completion line with its input root and count, in user terms — `found 2 video(s) under <root>`); TTY runs keep the live start line for feedback during long recursive scans. The `candidate file(s)` qualifier and the `(recursive=true)` code literal are dropped.
- The standalone `Probing complete: N file(s).` line is removed; the plan that follows carries the outcome.
- The plan collapses to a single line when no pending file carries measured probe data (`2 video(s) to encode at CQ 28 (probing skipped: short videos)`), naming the count, the CQ in effect, and why no measurements are shown; rows for unprobed files in mixed batches state their skip reason; totals print only when estimates exist.
- The `Scheduling N video(s) with max M concurrent encode job(s)...` line is removed — progress bars (TTY) and the summary (always) convey activity; concurrency remains visible via `-v` echo and `--jobs` help.
- The post-encode summary renders conditionally: full success prints the count line (`Encoded 2/2 videos → out`) plus the existing one-line preview hint; failures print the count line plus the failed-file list and existing "Needs attention" / `Compare:` hint lines only when they apply. The `All encoding tasks completed.` and `Summary:` header lines are removed.
- Picture mode stops announcing phases twice: the "will be compressed to JPEG" announcement merges into the single `Compressing N picture(s)...` start line, and the mechanics lines (`grouping into package batch(es)`, `N picture(s) prepared for packing, preparing pack plan...`) are removed.
- Narration trailing ellipses are pinned to ASCII `...` (already the de-facto standard across status lines; the mid-string filename truncation marker stays `…`, distinct from trailing ellipses), and the organize report adopts the plan's `─` rule glyph so all reports share one rule style.

## Capabilities

### New Capabilities

- `pipeline-narration`: which narration lines each pipeline phase prints (scan lines, phase announcements, mechanics lines) and their wording conventions (counts in user terms, ASCII ellipses, unified rule glyphs).

### Modified Capabilities

- `plan-output-formatting`: the probe-plan requirement gains the collapsed single-line form for batches without measured data; the post-encode summary requirement changes from a fixed five-line block to a conditional one-line success / failure-list form.
- `video-encode-probing`: the plan-presentation requirement allows the collapsed form and prints totals only when estimates exist; the short-video requirement changes from silent skip to a skip that the plan identifies (still no warning-level diagnostic).

## Impact

- `src/video/video_process.cpp` (scan lines, summary block), `src/video/video_batch_execution.cpp` (scheduling line, prompt flow), `src/video/encode_probe.cpp` (probing-complete line, plan collapse, unprobed-row reasons), `src/picture/picture_process.cpp` (announcements, mechanics lines), `src/pack/packer.cpp` (scan line wording), `src/organize/report.cpp` (rule glyph), `src/core/display_text.h` (ellipsis pinning tests).
- Ordering: lands before or after `failure-reason-visibility` independently — the summary's failed-file list gains per-file reason text from that change without format changes here.
- Tests asserting the removed/rewritten lines across unit and e2e suites are updated in the same change; progress-bar behavior is untouched (`progress-*` specs unchanged).
