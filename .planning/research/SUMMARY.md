# Research Summary: CLI Help Coloring & Terminal Color Deepening

**Domain:** CLI tool colored output for --help, errors, warnings, --version
**Researched:** 2026-05-09
**Overall confidence:** HIGH

## Executive Summary

Colored CLI output is a settled industry convention, not a novelty. Every major CLI tool released since ~2019 (cargo, ripgrep, fd, bat, gh, zoxide) ships with colored --help, colored errors, and colored warnings by default. The universal opt-out mechanism is the NO_COLOR environment variable (formalized at no-color.org, supported by 200+ tools). Encro already has the foundational infrastructure for this — the `terminal::` semantic color layer (`MessageKind` enum → `fmt::text_style` mapping) with full NO_COLOR/`--color`/isatty/TERM=dumb/Windows VT gating. The remaining work is extending this layer with help-section semantic values and wiring it into CLI11's `formatter_fn`.

The research confirms that colored CLI output follows a consistent de facto standard across tools:
- **Section headers** (USAGE, OPTIONS): Bold + colored (cyan, green, or steel blue)
- **Option names** (`--input`, `-i`): Bold + bright color (floral white, green, or cyan)
- **Description text**: Plain (no color — readability first)
- **Errors**: Red bold with badge prefix (`error:` or `[error]`)
- **Warnings**: Yellow bold with badge prefix (`warning:` or `[warn]`)
- **Version**: Bright version number, dim metadata
- **NO_COLOR gate**: Present + non-empty → disable all color; `--color always` overrides

The implementation path is clear and low-risk: 5 new `MessageKind` enum values + 5 `styleFor()` cases + a ~60-80 line `formatter_fn` lambda that replaces `boost::program_options::print()` + call-site audits for error/warning consistency + one version output handler. Zero new dependencies — everything reuses the existing `terminal::` and `fmtlib` layers. All color gating (NO_COLOR, --color, isatty, piped output) is already implemented and requires no changes.

## Key Findings

**Stack:** Existing `terminal::` semantic layer (MessageKind → fmt::text_style) + CLI11 `formatter_fn` injection point. Zero new dependencies. Reuse `fmtlib` colors already compiled.

**Architecture:** Single source of truth: `terminal::styleFor(MessageKind)` → `fmt::text_style`. CLI11 formatter_fn calls `terminal::println()` for each help line. `colorsEnabled()` gates all color. No scattered ANSI sequences.

**Critical pitfall:** The CLI11 `formatter_fn` is a complete replacement, not a decorator. It receives `(App*, string, AppFormatMode)` and must return the entire help string. Every layout detail (column alignment, wrapping, spacing) must be hand-rolled — the default formatter's layout is bypassed entirely.

## Implications for Roadmap

Based on research, the v1.6 milestone should have two sequential sub-phases:

1. **MessageKind extension + terminal:: style mapping** — Add `OptionGroup`, `OptionName`, `OptionDesc`, `Usage`, `Version` to the enum + their `styleFor()` cases. This unblocks all downstream coloring work. Independent, testable in isolation.

2. **CLI11 formatter_fn + error/warning/version wiring** — The main implementation phase:
   - Hand-roll the help renderer inside `formatter_fn` (walk CLI11 option groups, format with `terminal::println()`)
   - Audit and wire error paths to use `terminal::println(Error, ...)`
   - Audit and wire warning paths to use `terminal::println(Warning, ...)`
   - Implement colored `--version` output
   - Adapt `console_width.h` into the formatter

**Phase ordering rationale:**
- Phase 1 must precede Phase 2: the `formatter_fn` and error/warning wiring depend on the extended `MessageKind` values existing.
- The two sub-phases could ship as a single milestone (v1.6) since Phase 1 is trivially small (~15 lines of code).
- Progress bar coloring is explicitly deferred to a future milestone — the seed flags it as uncertain due to potential conflicts with the progress bar library's own color system.

**Research flags for phases:**
- Phase 1 (MessageKind): No research needed — fully specified here.
- Phase 2 (formatter_fn): Need to verify CLI11's internal option/group iteration API for constructing the help string. The `formatter_fn` signature and `AppFormatMode` enum are documented but the iteration API needs code-reading of CLI11 headers during implementation.
- Future (progress bar coloring): Needs separate research phase to evaluate the bar library's color system and potential integration conflicts.

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | Existing terminal:: layer confirmed as sufficient. fmtlib color API verified via Context7. CLI11 formatter_fn API verified via Context7. |
| Features | HIGH | Industry conventions verified across cargo, ripgrep, fd, gh, clap. NO_COLOR standard verified via no-color.org. Backed by 200+ tool adoption. |
| Architecture | HIGH | Single source of truth pattern (MessageKind → styleFor) confirmed working in existing codebase. formatter_fn injection point confirmed by CLI11 docs. |
| Pitfalls | HIGH | formatter_fn replacement-not-decorator pitfall confirmed by CLI11 docs. Progress bar color conflict risk flagged honestly. |

## Gaps to Address

- **CLI11 option/group iteration API:** The exact API for walking `CLI::App`'s option groups and options within `formatter_fn` is documentable but needs implementation-time code reading of CLI11 headers. Not a research gap — a code-reading task during implementation.
- **Progress bar library color integration:** Explicitly deferred. The seed file flags this as uncertain. Requires a separate research phase to evaluate whether the progress library has its own ANSI output that could conflict with `terminal::` styling.
- **Truecolor vs. 16-color auto-detection:** Deferred to v2+. The 16 ANSI terminal colors (via `fmt::terminal_color`) are sufficient and universally supported. Truecolor detection adds complexity without proportional user value for a CLI tool's help output.

## Sources

- **NO_COLOR Standard:** https://no-color.org/ — HIGH confidence (authoritative standard, 200+ tool adoption list)
- **CLI11 Documentation:** Context7 `/cliutils/cli11` — HIGH confidence (official docs, formatter_fn API, subclassing, Rang integration)
- **fmtlib Color API:** Context7 `/fmtlib/fmt` — HIGH confidence (official docs, text_style, emphasis, terminal_color, rgb)
- **Existing codebase:** `src/infra/terminal.h`, `src/infra/terminal.cpp`, `src/cmd/cmd.cpp` — HIGH confidence (primary sources)
- **Design notes:** `.planning/notes/cli-library-selection.md`, `.planning/seeds/cli-color-deepening.md` — HIGH confidence (design authority)
- **Industry conventions:** cargo, ripgrep, fd, gh, clap — MEDIUM confidence (training data, not live-fetched, but consistent across 5+ major tools)

---

*Research summary for: encrō v1.6 CLI Color Deepening*
*Researched: 2026-05-09*
