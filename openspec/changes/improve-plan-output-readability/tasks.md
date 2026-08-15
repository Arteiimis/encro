# Tasks: Plan Output Readability

## 1. Shared Formatting Helpers

- [ ] 1.1 Add `src/core/display_text` helpers (or extend existing): `formatSignedPercent(double ratio) -> std::string` (`−20%`/`+24%`, ASCII `-` fallback, `↑` marker when ratio > 1.0), `truncateMiddle(text, maxWidth)` (Unicode-width aware, extension preserved), and `layoutColumns(terminalWidth)` returning the per-column widths/unit mode for the plan table (bands: full table ≥70 cols, numeric-shrink 50–69, nullopt below 50); unit tests for each (`[cmd]` or a display-text tag)

## 2. Probe Plan Table

- [ ] 2.1 Rewrite `printProbePlan` in `src/video/encode_probe.cpp`: partition normal/unreachable, sort each group by file name (UTF-8 byte order), print the unreachable count line, build header + one row per file (CQ, p5 one decimal, auto-scaled size, signed-percent ratio), emit the body as one plain multi-line write (no badge), and update the `Total:` line to the signed-percentage ratio; unit tests asserting exact output at fixed widths (`[encode-probe]`)
- [ ] 2.2 Apply the two-line fallback path when `layoutColumns` returns nullopt (very narrow terminals), keeping all data; unit test for the fallback string (`[encode-probe]`)

## 3. Post-Encode Summary

- [ ] 3.1 Restructure `printEncodingSummary` in `src/video/video_process.cpp` to the same formatting conventions (counts/lists alignment, signed-percent via the shared helper where ratios appear) without adding or removing content; update any unit/e2e assertions that match summary text (`[e2e]`, `[video-batch-execution]`)

## 4. Verification

- [ ] 4.1 Update existing tests that assert plan/summary text (`extractPlanCq` in e2e, plan-line assertions in encode-probe unit tests) to the new format; add e2e coverage for a multi-file plan with an unreachable floor asserting the grouped table shape (warning rows last, count line, signed percentages)
- [ ] 4.2 Run `xmake format -k`, full unit + e2e suites, and a real-terminal manual check of the plan on a batch with long file names and CJK names
