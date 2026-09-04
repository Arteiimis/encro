## Context

`encro -h` is rendered by a custom `formatter_fn` (src/cmd/cmd.cpp); CLI11's default formatter is bypassed entirely. Name rendering is centralized in `formatOptionName()`, which concatenates long names first, then shorts, into one string; `formatOptionHelp()` pads that single first column using a width from `computeMaxColumnLen()` (max of name + `(=default)` across visible options) and `computeColumnWidth()` (git-style auto-fit with a COLUMNS-derived cap). Main and subcommand helps share these helpers. CLI11 exposes `get_snames()`/`get_lnames()` separately, which `formatOptionName()` already reads.

## Goals / Non-Goals

**Goals:**

- Short-first rendering with two aligned sub-columns (short cell, long cell) in every help table.
- Keep all existing layout invariants: auto-fit + COLUMNS cap, 3-space gap, 2-space indent, color-mode invariance, wrapping behavior.

**Non-Goals:**

- No changes to parsing, `-hh` tiering membership, usage block, commands section, or shell-completion name generation (independent, reads `get_lnames()`/`get_snames()` directly).
- No per-option color changes.

## Decisions

- **Split `formatOptionName` into short-cell and long-cell helpers instead of reordering the single string.** The two-cell layout needs the parts separately padded; returning one concatenated string cannot align `--version` under `--help`. Long-cell logic (multi-long-name loop, `no-` collapse, positional `get_name(true)`) moves over unchanged.
- **Short cell is constant width, not auto-fit.** All registered short names are single characters, so the cell is exactly `"-x, "` (4 chars) or 4 blanks. No second max computation; if a multi-char short name ever appears, widen via the same max loop (upgrade path, not built now).
- **Auto-fit measures only the long cell.** `computeMaxColumnLen` switches from `formatOptionName(opt).size() + default` to `longCell(opt).size() + default`; `computeColumnWidth`, gap, indent, and wrapping math are untouched.
- **Positional options keep single-name rendering** in the long cell (they have no flags); short cell is blank for them.
- **Separator is `", "` after the short name**, matching the agreed format sketch (`-h, --help`).

## Risks / Trade-offs

- [Tests lock the current long-first strings (`--help,-h`, `--[no-]pack,-p`, widest-column `--output-format,-f (=mp4)`)] → update assertions first (TDD), including recomputed widest-column text; layout-math tests (`nameEnd`, COLUMNS cap) keep their structure.
- [Constant short-cell width silently breaks if a multi-char short name is added] → single registration site; a new multi-char short shows up as a misaligned column in any help snapshot test.
- [Subcommand positionals (`preview original/encoded`) now sit in the long cell] → they already render via `get_name(true)`; only their column shifts.

## Migration Plan

Single commit (tests + implementation + tasks checkbox per repo convention). No data or API migration; help output is the only observable change.

## Open Questions

(none)
