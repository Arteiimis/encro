## 1. Scan narration

- [x] 1.1 Add failing tests: a piped (non-TTY) video scan prints exactly one scan line naming the count (`found 2 video(s)`, no `candidate` qualifier), a pack scan drops `(recursive=true)`, and a TTY scan keeps its start line (drive the TTY gate through the existing terminal/stream abstraction). Then update `src/video/video_process.cpp` and `src/pack/packer.cpp` scan narration per design D1/D4 and verify with the video/pack unit suites (piped branch covered by unit + e2e captures; the TTY start-line branch is gated by `terminal::streamIsTerminal` and has no automated test — no TTY harness exists in this repo, acceptance is by code inspection)
- [x] 1.2 Add failing tests for the picture-mode single-announcement rule (one `Compressing N picture(s) to JPEG (quality=q)...` line, no "will be compressed" pre-announcement, no `grouping into package batch(es)` / `preparing pack plan` lines in captured output). Then remove the duplicated announcement and mechanics lines in `src/picture/picture_process.cpp` (design D4, pipeline-narration R2/R3) and verify with the picture unit suite

## 2. Plan collapse and probing lines

- [x] 2.1 Add failing tests to the encode-probe plan suite: an all-short-video batch renders the collapsed single line (`2 video(s) to encode at CQ 28 (probing skipped: short videos)`) with no rule/header/totals lines; a mixed batch keeps the table with a skip note on unprobed rows; no standalone `Probing complete` line prints in any mode. Then implement the collapse predicate and skip-note suffixes in `src/video/encode_probe.cpp` (design D2) and verify with `xmake test-report --tag="[encode-probe]"` plus the `[plan-output]` tests
- [x] 2.2 Add a failing test that totals do not print and no ratio is computed when no estimates exist (no `−100%` against an empty estimate); fix the totals emission in the plan renderer and verify in the same suite

## 3. Summary reshape

- [x] 3.1 Add failing tests for the conditional summary: full success prints the count line (`Encoded 2/2 videos → <dir>`) plus the existing one-line preview hint; a failing run prints the count line with the failing count plus the failed-file list; "Needs attention" and the preview `Compare:` hint appear only when they have entries; `All encoding tasks completed.` and `Summary:` never print. Then rebuild the summary block in `src/video/video_batch_execution.cpp`/`src/video/video_process.cpp` (design D3), leaving the failed-list entries as plain paths so per-file reasons from `failure-reason-visibility` slot in unchanged, and verify with the unit summary tests plus the e2e summary assertions (`tests/e2e/encro_e2e_tests.cpp`, updated in task 5.1)
- [x] 3.2 Remove the `Scheduling N video(s) with max M concurrent encode job(s)...` console line in `src/video/video_batch_execution.cpp` (the info-level batch-start log record stays; design D6) and update the e2e assertions that captured it; verify with the video e2e suite

## 4. Wording conventions

- [x] 4.1 Pin the narration ellipsis conventions with a `src/core/display_text.h` unit test: trailing status ellipses are ASCII `...` (already the de-facto standard — this locks it in rather than sweeping), truncation marker `…` untouched (design D5); switch the organize report rule lines in `src/organize/report.cpp` from `-----` to `─` and update its report tests

## 5. End-to-end verification

- [x] 5.1 Run `xmake build e2e_tests && xmake run e2e_tests` fixing narration-assertion drift, then `xmake test-parallel` to confirm no regressions across unit + e2e suites
