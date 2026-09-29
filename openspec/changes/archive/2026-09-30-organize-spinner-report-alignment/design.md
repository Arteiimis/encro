## Context

See proposal.md for motivation and measurements. The three pieces of current state that shape this design: `ProgressContext` (src/core/progress.{h,cpp}) renders `indicators::ProgressBar` widgets through `DynamicProgress` on an internal repaint clock (`tick()`, `kRepaintInterval`) and owns the bar lifecycle (`eraseBars()`); `encro organize` builds both ONNX engines before creating any progress context, so model loading is silent; the run report (src/organize/report.cpp) renders its folder table with `std::format("{:<30} ...")`, which pads by bytes and never truncates, while the encode probe (src/video/encode_probe.cpp) already has the target layout: terminal-width-aware column budget, `padToDisplayWidth`, truncation.

## Goals / Non-Goals

**Goals:**

- An indeterminate spinner mode on `ProgressContext` reusable by any future phase with unknown-duration work.
- organize engine loading shows the spinner on a TTY; the one provider notice prints after the spinner clears.
- The organize folder table aligns under all inputs: dynamic folder-column width, display-width padding, ellipsis truncation.

**Non-Goals:**

- Provider-negotiation or engine-construction performance changes. Measured on the target machine (CUDA provider succeeds on the first attempt), the ladder has no wasted attempts; cold-start cost is CUDA/cuDNN DLL loading, which this change makes visible, not fast.
- No new progress widget type in the terminal layer; no changes to determinate bar rendering.

## Decisions

### D1: Spinner = per-bar indeterminate flag on `ProgressContext`, not a new widget

`indicators` ships `IndeterminateProgressBar`/`ProgressSpinner`, but `ProgressContext` manages widgets as one homogeneous `DynamicProgress<ProgressBar>` collection; a second widget type means restructuring the render/erase path for one user. Instead: a per-bar indeterminate flag (parallel to `roles_`/`postfixes_`, set via `setIndeterminate(barIndex, bool)`), animated by `tick()` which already repaints on the context clock. The flag flips animation responsibility only; add/erase/postfix/role plumbing is untouched.

Animation, driven by the existing clock with no per-bar mutable state: a rotating Braille glyph (⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏) prefixed to the postfix text, phase from `tickCount_`. This is the entire animation — the bar's progress value is never touched from `tick()`, because the scroll-label spec pins repaints to display-only (`a repaint SHALL NOT change a bar's progress value`); a fill sweep would require a delta on that spec and buys nothing beyond the rotating glyph the spinner needs. Glyph phase is computed by a small free function next to `bounceOffset` so it is unit-testable without a TTY.

Because the estimator is never sampled for an indeterminate bar, `applyBarText` gets no elapsed sample and the ETA badge stays off with zero extra code. Alternatives rejected: `IndeterminateProgressBar` widget — bigger integration, single user today; a fill sweep driven by the widget's `set_progress` — violates the scroll-label spec's display-only repaint rule (see above).

### D2: Spinner lives in its own `ProgressContext`, ahead of engine construction

The lifecycle spec forbids rendering after a clear and prescribes a fresh context for a new phase, and requires bars removed before any failure diagnostic. So `makeEngines` stops printing engine-build errors itself: it collects them, and `runOrganizeCommand` creates the spinner context, runs `makeEngines`, erases the spinner, then prints the collected errors (if any) and the provider notice — then the existing per-run context is created where it is today for the analysis bar. The spinner is skipped for the fake-engine path (nothing loads) and is a no-op off-TTY (`renderable()`/`progressBarsAllowed()` already gate rendering).

### D3: Report table follows the encode-probe layout pattern

`renderReport(ReportData const&, std::size_t terminalColumns)` — the caller resolves the width (`consolewidth::resolveColumns()`), keeping the renderer pure and the layout deterministic for tests (the probe resolves at its call site for the same reason).

Column math: tail after the folder name is `1 + 6 (images) + 2` plus the source label; the longest source label (`character tag`, `uncategorized` = 13) sets the worst-case row tail, so `nameWidth = clamp(maxFolderNameDisplayWidth, 30, terminalColumns - 9 - 13)`, with the current fixed 30 as the fallback when width resolution yields nothing usable. The floor of 30 preserves today's rendering for short-name tables (the encode probe keeps a floor of 20 for the same reason); only names wider than the budget truncate. Names wider than `nameWidth` go through `truncateWithEllipsis` (head truncation; folder names carry no extension, so the probe's `truncateMiddle` has nothing to preserve). Padding uses `padToDisplayWidth`, hoisted from `encode_probe.cpp` into `core/display_text.h` next to its width siblings (pure move; probe behavior unchanged). The header row is rendered through the same padding path as data rows, and the `boxRule` width is computed from the rendered header instead of the hard-coded 46, keeping the rule-glyph convention (`pipeline-narration`) intact.

### D4: Byte-based `std::format` padding is replaced, not wrapped

`std::format("{:<30}")` pads by code units and cannot truncate. Rather than pre-truncating and keeping `{:<30}`, every cell is rendered with `padToDisplayWidth` so CJK tag names (possible via tag vocabulary) align by display width, matching the probe table and the display-text utilities' contract.

## Risks / Trade-offs

- [Braille glyph width varies across fonts] → same exposure as existing U+2500 rules and the U+2026 ellipsis; the glyph set is the standard ten-frame spinner that monospace fonts treat as single-width. Swap is a one-constant change.
- [Determinate bars share `tick()` with spinning ones] → flagged bars take the animation path (postfix glyph rotation via the same display-option rewrite `applyBarText` already does), unflagged bars keep exactly their current `applyBarText(index, lastProgress())` call — behavior-preserving for existing users.
- [Two contexts in one run doubles cursor bookkeeping] → each context only erases what it rendered (`renderedBarCount_`); the spinner context is erased before any output follows it, matching the lifecycle spec's "clear before summary line" rule.
