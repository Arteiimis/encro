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
  already is for the severity-prefix convention; its level colors come from the echo
  sink's own configuration.
- Move styling from whole message bodies onto tokens: a status line's leading verb,
  paths, counts, and option names carry a role; the surrounding prose stays in the
  terminal's default foreground. The progress-bar tones that exist only as separate
  names for one color collapse, and idle bars take the active color instead of a fixed
  white — the bar library cannot leave one bar uncolored inside a colored frame, so a
  per-bar default was never available and today's white is what breaks on a light
  background.
- Collapse the two styling entry points (`format`/`renderMessage` by message kind, and
  `styledText` by message kind) into one primitive over named styles: a role-to-style
  mapper, a bold style for help headings, and the accent helper, with the eleven help
  call sites rewritten against them. No code outside that module emits a foreground
  style.
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
- Remove the message kinds with no live callers. Four were already dead or single-use
  (`Heading` 0, `Usage`, `Version`, `Prompt` 1 each); four more (`OptionGroup`,
  `OptionName`, `OptionDefault`, `OptionDesc`) are left dead by collapsing the
  kind-keyed styling entry point onto the help formatter's own call sites. The enum ends
  at the seven kinds that still have callers.
- Fix progress-bar coloring when colors are disabled: `--color never` currently forces
  bars to `Color::white`, which is unreadable on a light background. With styling
  disabled every bar must resolve to no color, which leaves the whole frame in the
  terminal's default foreground.

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
  nested". The stream-routing requirement is restated to name the attention block
  generically, since the change retires its `Needs attention:` label. The capability also
  gains the diagnostic-grouping requirement that retires the repeated `warning:` marker.
- `plan-output-formatting`: the post-encode-summary requirement names the
  `"Needs attention"` list and its scenarios quote that literal. It is restated to refer
  to the attention block defined by `console-output-conventions`, so archiving does not
  leave two main specs naming the same output differently.

## Impact

- **Console rendering**: `src/infra/terminal.{h,cpp}` (role table, the two role mappers,
  token styling, `renderMessage`, `severityPrefix`, the message-kind switches, and the
  `styledText` → `styled`-plus-named-styles replacement), `src/cmd/cmd.cpp` (help option
  tables, group headings, the commands section, and the whole-line brief-tier hint).
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
  by behavior assertions, and the `styledText` cases move to `styled`),
  `tests/infra/progress_tests.cpp` (the colors-disabled expectation flips), the one
  `styledText` use in `tests/test_utils_tests.cpp`, plus any test asserting on a removed
  kind.
- **Specs**: new `openspec/specs/terminal-color-palette/`, modified
  `openspec/specs/console-output-conventions/` and
  `openspec/specs/plan-output-formatting/`.
- **Not affected**: the `--color auto|always|never` CLI surface, the `color` user-config
  key, `NO_COLOR`, stdout/stderr routing, `--quiet` gating, help column layout, and log
  file contents.
