## 1. Indeterminate spinner mode on ProgressContext

- [ ] 1.1 Add the spinner animation helper as a free function next to `bounceOffset` in src/core/progress.cpp (glyph-phase selector over the ten-frame Braille set, phase from tick count) with unit tests covering phase advance and wrap-around — verified by the new `[progress]` test cases passing in `xmake test-report`
- [ ] 1.2 Add the per-bar indeterminate flag to `ProgressContext` (`setIndeterminate(barIndex, bool)` plus an observer for tests) and drive flagged bars from `tick()`: rotating glyph prefixed to the postfix via the existing display-option rewrite, progress value untouched so no ETA sample is produced — verified by a unit test asserting consecutive ticks advance the glyph phase and that no ETA badge is produced for the indeterminate bar
- [ ] 1.3 Keep determinate and lifecycle behavior unchanged: mixed-context test (one indeterminate + one determinate bar renders the determinate bar's progress/ETA as before) and clear test (erased indeterminate bar never draws again; off-TTY writes no frames) — verified by the corresponding `[progress]` unit tests

## 2. Engine-loading spinner in organize

- [ ] 2.1 Wire the spinner into `runOrganizeCommand`: fresh `ProgressContext` before `makeEngines` (fake-engine path skips it entirely), one bar marked indeterminate ("Loading models"), spinner erased before the provider notice; `makeEngines` stops printing build errors itself and returns them, so the caller erases the spinner before printing them — verified by the fake-engine unit path asserting unchanged behavior (no provider notice, run still succeeds) plus a `[real-model]` smoke case asserting the run succeeds and exactly one provider notice prints; the spinner animation itself is covered by the task 1.x primitive tests

## 3. Report table alignment

- [ ] 3.1 Move `padToDisplayWidth` from src/video/encode_probe.cpp to src/core/display_text.h (pure relocation) — verified by the encode-probe tests staying green with no behavior change
- [ ] 3.2 Change `renderReport` to take the terminal-column budget, compute the folder-column width as `clamp(max folder-name display width, 30, columns - 9 - 13)` with the fixed-width fallback, render all cells (header included) through `padToDisplayWidth`, truncate over-wide folder names with `truncateWithEllipsis`, and derive the `boxRule` width from the rendered header row; update the call site to resolve columns via `consolewidth::resolveColumns()` — verified by new `renderReport` unit test cases: a 39-char folder name is truncated and every row's images/source columns align with the header, a narrow run keeps the column at its minimum width, and a CJK folder name aligns by display width

## 4. Wrap-up

- [ ] 4.1 Full suite green (`xmake test-report`), reporter-mode probe clean (`build/windows/x64/release/tests.exe -r console -s` reports 0 failures), `xmake fmt` produces no diff on touched files

## 5. Planning review

Fresh-reviewer pass over the four artifacts (Coherence + Ground-truth lenses), 6 findings, all fixed in this planning pass and verified by a second fresh reviewer — no rejections, no regressions:

- resolved: "Narrow tables stay compact" scenario contradicted the width floor (spec delta rewritten to "stays at the minimum width").
- resolved: task 2.1 verification asserted unobservable behavior (spinner rendering / notice ordering under fake engine); reworded to assertable checks.
- resolved: design D2 claimed errors print after spinner erase while `makeEngines` printed them itself; `makeEngines` now collects errors, caller prints after erase.
- resolved: same root as above — impossible fake-engine provider-notice assertion removed.
- resolved: design D1's fill sweep violated progress-scroll-label's display-only repaint rule; animation is now postfix-glyph-only, progress value untouched.
- resolved: proposal misdescribed the current renderReport test as asserting fixed-width format; corrected to content substrings.
