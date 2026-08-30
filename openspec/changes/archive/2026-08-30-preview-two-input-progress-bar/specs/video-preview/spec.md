## ADDED Requirements

### Requirement: Two-input preview shows one progress bar and reports after render

Two-input preview SHALL show a single progress bar spanning the whole pipeline — input probing (0-10%), window scoring (10-85%), comparison render (85-100%) — in the same style as the encode bars, with the terminal cursor hidden while it runs (non-TTY output renders nothing). The bar SHALL advance as each window is scored, with a postfix showing scored-window progress. Under `--start/--duration`, which skips scoring, the bar SHALL move from probing directly to the render segment and still complete at 100%. The summary (window list with the worst window marked and the written-to line) SHALL be printed only after the render completes, exactly once per run. On render failure the bar SHALL switch to a failure tone with a failure label.

#### Scenario: One bar spans probe through render

- **WHEN** the user runs `encro preview a.mp4 a.hevc.mp4` on videos that require sampled windows
- **THEN** one progress bar advances through probing, advances per scored window through scoring, moves to the render segment, and completes at 100%

#### Scenario: Manual range skips the scoring segment

- **WHEN** the user runs two-input preview with `--start` and `--duration`
- **THEN** the bar moves from probing straight to the render segment and completes at 100%

#### Scenario: Summary appears after the render

- **WHEN** two-input preview finishes rendering
- **THEN** the window score list and the written-to line are printed after the render completes, once, and no score list is printed before the render

#### Scenario: Render failure marks the bar

- **WHEN** the comparison render fails in two-input mode
- **THEN** the bar switches to a failure tone with a failure label and the error is reported
