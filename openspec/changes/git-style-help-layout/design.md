# Design: git-style-help-layout

## Context

All help rendering lives in `src/cmd/cmd.cpp` as CLI11 `formatter_fn` callbacks: one for the main app (`makeHelpFormatter`) and one shared by the subcommands (`makeSubcommandHelpFormatter`). Both duplicate an identical column-width block: `colWidth = clamp(maxColumnLen, min(34, maxColWidthFromLayout), min(48, maxColWidthFromLayout))`, where `maxColumnLen` is the widest first-column text (option name + `(=default)`) across all visible options and `maxColWidthFromLayout` is the `COLUMNS`-derived cap (lineLength − lineLength/2 − 2). The 34 lower clamp is what pushes descriptions to column 37 regardless of content. `formatOptionHelp` pads the first column to `colWidth` and wraps descriptions to `lineLength − column`; it has two column notions: `displayDescriptionColumn` (plain text) and `renderedDescriptionColumn` (ANSI byte length), the latter used for first-line wrapping only when `COLUMNS` is set.

Subcommand names and descriptions are already registered on the CLI11 app (`preview` / `config` via `add_subcommand`); no new strings exist for the commands section to introduce.

## Goals / Non-Goals

**Goals:**

- One column-width rule shared by the main and subcommand formatters.
- Description column that adapts to what is actually rendered (per tier, per subcommand).
- A commands section rendered from the registered subcommand apps (single source of truth).
- Layout that does not depend on the color mode.

**Non-Goals:**

- No change to option registration, parsing, tiering contents (which options are advanced), the hint line, or subcommand usage lines.
- No per-group column alignment; no new terminal color kinds.
- No unification of the `-h` and `-hh` column position (see D2).

## Decisions

- **D1 — Column formula: `colWidth = min(widest + 3, maxColWidthFromLayout)`**, replacing the 34..48 double clamp. The `+3` makes the widest line's gap exactly 3 via the existing `gap = colWidth − size` arithmetic. When the narrow-terminal cap bites (cap < widest + 3), the widest line falls back to `formatOptionHelp`'s existing 2-space minimum gap — acceptable where space is scarce, and the spec only pins the gap in the uncapped case. Alternative considered: enforce a 3-space gap even when capped (harder cap, potentially tighter wrapping) — rejected as unnecessary for the narrow-terminal path that only two tests exercise.
- **D2 — Per-tier auto-fit** (user decision): each rendered help measures its own visible options, so `-h` (widest `--output-format,-f (=mp4)`, description column 31, 1-indexed — tests pin the 0-indexed offset 30) and `-hh` (widest `--force-conflict-handling (=y)`, column 36 / offset 35) differ by 5 columns. Alternative considered: always use the full-tier width for both tiers for stable columns across tiers — rejected as less literal to the auto-fit request and because it wastes width in the primary `-h` view.
- **D3 — Global width across groups** (matches `git -h`, which aligns descriptions across all category groups): reuse `computeMaxColumnLen` as-is. Per-group widths would leave `-hh` groups starting at columns 21 / 35 / 32 / 19 — ragged when scanning.
- **D4 — `(=default)` counts toward the width basis**, as today: the first column is name + default, and both are padded together. Excluding defaults would align names while letting `(=…)` tails eat into the gap.
- **D5 — Commands section from registered apps**: the section is given the real subcommand apps explicitly (`previewSub` / `configSub` via a span into `makeHelpFormatter`) and renders rows from `get_name()` / `get_description()` — single source of truth, no duplicated strings. CLI11 offers no clean way to enumerate "real" subcommands from the app: the no-argument `get_subcommands()` returns only the subcommands parsed from the current command line (empty for `encro -h`), and the filtered overload also returns the option groups, which are `App` subcommands internally — rendering them leaked four group rows into the section, caught by visual inspection and now pinned by the whole-block test. Coloring reuses the existing message kinds (`OptionGroup` header, `OptionName` / `OptionDesc` cells) — no new terminal kinds. Alternative considered: hard-coded strings duplicated in the formatter — rejected (descriptions would drift from the subcommand help).
- **D6 — Slim usage in the main formatter only**: remove the `encro preview …` and `encro config …` lines from the main usage array; insert the commands section right after the usage block. `kPreviewUsageLines` / `kConfigUsageLines` stay untouched (spec: subcommand help keeps its own usage line). The tier-advertising `encro -h | -hh | --version` line and the positional-form line stay in the main usage.
- **D7 — Extract one column helper**: a single `computeColumnWidth(widest, layout)` used by both formatters, replacing the duplicated clamp block. Pure refactor of the duplicate; no behavior split.
- **D8 — Color invariance fix: wrap from `displayDescriptionColumn` always.** Delete the `renderedDescriptionColumn` computation and the `explicitWidthConstraint` plumbing (field in `HelpTextLayout`, parameter of `formatOptionHelp`). The old byte-based first-line width existed to keep raw byte length under the `COLUMNS` cap when colored; it trades visual correctness for a property no test asserts (the `longestHelpLine` tests run without color enabled). The new color-invariance scenario pins the display-width contract.

## Risks / Trade-offs

- [-h and -hh columns differ by 5] → Accepted deliberately (D2); each view is internally aligned; flipping to unified later is a one-line change in the D1 helper.
- [Existing tests may silently keep passing while pinning nothing new] → New tests pin the 3-space gap on the widest line of each tier, the commands section, the slimmed usage, and color invariance (see tasks).
- [The aliases continuation under `--output,-o` shifts left with the narrower column] → Intended: continuation indent is `displayDescriptionColumn`, so the block stays glued to its description column everywhere.
- [`wrapDescriptionLine` is width-generic and unchanged] → No new wrap edge cases expected; the keep-description line (~85 chars) now fits one line at the default 120 width instead of wrapping.

## Migration Plan

Single binary, no persisted state: build, run `xmake test-parallel`, visually check `encro -h`, `encro -hh`, `encro preview -h`, `encro config -h`. Rollback is `git revert` of the single implementation commit.

## Open Questions

None.
