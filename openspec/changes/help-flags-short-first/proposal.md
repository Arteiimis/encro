## Why

The current help renders option names long-first (`--[no-]pack,-p`), which buries the short flags most users actually type. Short flags should lead, and short and long names should each align in their own column so the eye can scan either list vertically.

## What Changes

- Reorder rendered option names from long-first to short-first: `-p, --[no-]pack` instead of `--[no-]pack,-p`.
- Split the option-table first column into two aligned sub-columns: a fixed-width short-flag cell (`-x, `, blank when the option has no short name) followed by the long-name cell (`--long`, with the existing `--[no-]` collapse and `(=default)` suffix).
- Options without a short name (`--version`, `--state-file`, `--restart`, `--min-vmaf`, ...) render an empty short cell so all long names align in one column.
- Column auto-fit now measures the long-name cell (long name plus `(=default)`); the short cell is constant width because all short names are single characters.
- Applies to the main help and all subcommand helps (they share the same format helpers); usage block, `-hh` tiering filter, commands section, and color-mode invariance are unchanged.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `cli-help-layout`: the auto-fit requirement's "first column" becomes two sub-columns (short flags, long flags); auto-fit width is computed over the long-name cell only.
- `cli-help-tiering`: the collapsed-negation requirement switches from the long-name-first convention (`--[no-]pack,-p`) to short-first rendering (`-p, --[no-]pack`).

## Impact

- `src/cmd/cmd.cpp`: `formatOptionName`, `formatOptionHelp`, `computeMaxColumnLen` (only file with rendering logic; subcommand formatters inherit via shared helpers).
- Tests: `tests/cmd_cmd_tests.cpp` (name-format and widest-column assertions), `tests/cmd_help_tiering_tests.cpp` (collapsed-negation assertion).
- No CLI parsing, completion-script, or config changes: shell completion generates names independently from `get_lnames()`/`get_snames()`.
