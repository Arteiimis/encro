## Why

The console palette is assembled from four vocabularies that know nothing about each
other: `terminal::MessageKind` (15 values), `terminal::styleForToken` (2 values),
`progress::Tone` (8 values), and the spdlog console sink (3 values). Together they emit
eleven distinct foreground sequences, five of them hardcoded truecolor values that
measure 1.04–2.24:1 contrast on a light-background terminal. Because a style is applied
by concatenating strings rather than by composing values, an embedded token's reset
terminates the enclosing line's style: a real run prints `Found 2 file(s) under <path>`
with the body falling back to the terminal default immediately after the count.

Fixing the palette also removes surface that has no callers: four of the fifteen message
kinds have one production call site or none, and one near-white token color is used
exactly once.

## What Changes

- Replace the four vocabularies with one six-role semantic palette — `Default`, `Muted`,
  `Accent`, `Good`, `Warn`, `Bad` — expressed only in ANSI-16 slots. Every hardcoded
  truecolor value (`steel_blue`, `slate_gray`, `light_sky_blue`, `golden_rod`,
  `floral_white`) is removed, so the palette follows the user's terminal theme and stays
  legible on light and dark backgrounds alike. The verbose echo stream is exempt, as it
  already is for the severity-prefix convention; its level colors belong to
  `logging-behavior`.
- Move styling from whole message bodies onto tokens: a status line's leading verb,
  paths, counts, and option names carry a role; the surrounding prose stays in the
  terminal's default foreground. The four progress-bar tones that exist only to be
  distinct names for the same color collapse onto the shared roles.
- State the structural invariant that makes the truncation class of bug impossible:
  a styled token is never nested inside a styled message body.
- Render help group headings with bold instead of color, so help structure stops
  competing with option names for the same hue.
- Group diagnostics under one severity marker: one announcement line names the item
  count and carries the severity prefix once, and its items become indented plain lines
  (`<subject>: <reason>`), matching the shape of the failed-file list, instead of every
  line repeating `warning:` and the block reading as a label followed by a colon.
- Collapse the three token styles onto one accent: paths, counts, and other named
  values all use `Accent`. The string-accenting helper keeps its role (renamed, since it
  no longer means "value style") and the `completion` installer's path arguments are
  routed through it, so paths are accented there too rather than only everywhere else.
- Remove the message kinds with no live callers: `Heading` (0 call sites) and `Usage`,
  `Version`, `Prompt` (1 each).
- Fix progress-bar coloring when colors are disabled: `--color never` currently forces
  bars to `Color::white`, which is unreadable on a light background. Disabled colors
  must leave the bar in the terminal's default foreground. An idle bar stays in the
  default foreground with colors enabled too, so it renders the same on every platform.

## Capabilities

### New Capabilities

- `terminal-color-palette`: the six semantic roles, the ANSI slots behind them, what each
  role may be applied to across messages, styled tokens, help text, and progress bars,
  and the invariants that hold when styling is disabled or when tokens appear inside a
  message.

### Modified Capabilities

- `console-output-conventions`: the severity-prefix requirement currently states that
  color decorates the prefix only. Under the new palette a message body may also contain
  role-styled tokens (paths, counts), so that requirement is restated as "the prefix is
  role-styled and survives color disabling; embedded tokens are role-styled and never
  nested". The capability also gains the diagnostic-grouping requirement that retires the
  repeated `warning:` marker.

## Impact

- **Console rendering**: `src/infra/terminal.{h,cpp}` (role table, token styling,
  `renderMessage`, `severityPrefix`, the message-kind switches), `src/cmd/cmd.cpp`
  (help option tables and group headings).
- **Progress bars**: `src/core/progress.{h,cpp}` (tone-to-role mapping, the
  colors-disabled fallback, the `addBar` default role).
- **Run summary**: `src/video/video_process.cpp` (the attention block's announcement and
  item lines).
- **Call sites updated to the surviving kinds**: `src/app/app_entry.cpp`,
  `src/app/pipeline.cpp`, `src/utils/utils.cpp`, `src/video/video_batch_execution.*`,
  `src/picture/picture_process.cpp`, `src/preview/preview_process.cpp`, `src/pack/*`,
  `src/organize/*`, and `src/cmd/completion_install.cpp` (path arguments routed through
  the accent helper).
- **Tests**: `tests/infra/terminal_tests.cpp` (the per-kind expectation table is replaced
  by behavior assertions), `tests/infra/progress_tests.cpp` (the colors-disabled
  expectation flips), plus any test asserting on a removed kind.
- **Specs**: new `openspec/specs/terminal-color-palette/`, modified
  `openspec/specs/console-output-conventions/`.
- **Not affected**: the `--color auto|always|never` CLI surface, the `color` user-config
  key, `NO_COLOR`, stdout/stderr routing, `--quiet` gating, help column layout, and log
  file contents.
