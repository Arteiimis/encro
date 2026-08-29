## 1. Bar in two-input mode

- [ ] 1.1 In `run()`'s two-input branch of `src/preview/preview_process.cpp`, create `progress::ProgressContext` + `progress::CursorGuard` and add one bar `Previewing: <original filename>`; advance it 0-10% across the two input probes (postfix `Probing: <file>` per input).
- [ ] 1.2 Add an optional `BarSlot`-style parameter to `scoreComparisonWindows` and update the bar per scored window (progress `10 + 75 * done/total`, postfix `Scoring windows: N/M`).
- [ ] 1.3 Set the bar to 85% with postfix `Rendering comparison video...` before the render; on success complete at 100% with `Tone::Success` + `Preview complete`; on render failure set `Tone::Failure` + `Preview generation failed` (mirror `renderAndReportSingleInput`). Under `--start/--duration` skip the scoring segment (probe → 85% directly).

## 2. Summary timing

- [ ] 2.1 Remove the pre-render `printWindows` call in the two-input branch so the score list and written-to line print once, after the render.

## 3. Verification

- [ ] 3.1 Add a fake-toolchain e2e test: two-input preview on a sampled (≥50s probed duration) video asserts the run succeeds and `Preview windows` appears exactly once in stdout, after the render artifacts are written.
- [ ] 3.2 Run `xmake test-report` (unit) and the e2e suite; manually verify TTY bar behavior with a real two-input run.
- [ ] 3.3 Run `xmake fmt` and `xmake tidy`; confirm no new clang-tidy findings in touched files.
