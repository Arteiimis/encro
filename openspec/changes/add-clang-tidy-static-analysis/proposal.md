# Add Clang-Tidy Static Analysis

## Why

The project already enforces formatting (clang-format, `xmake format -k` + pre-commit hook) and include hygiene (clang-include-cleaner), and guards runtime safety with ASan and `_MSVC_STL_HARDENING`. The layer in between — static detection of bug patterns, API misuse, and complexity growth — is uncovered. A report-only clang-tidy scan closes that gap by giving developers and coding agents a per-warning fix list, without gating merges.

## What Changes

- **New `xmake tidy` command**: runs clang-tidy over every translation unit in `build/compile_commands.json` using a curated check set, printing warnings as text by default or as SARIF with `--sarif`. The slow path-sensitive static analyzer is opt-in via `--analyzer`; the default run is a fast scan (~2.5 min) of the bug-pattern/performance/portability checks plus function-length and cognitive-complexity guardrails.
- **Repo-root `.clang-tidy` config**: the single source of truth for the check set and thresholds (function length, cognitive complexity).
- **Project-code-only scoping**: warnings are reported only from `src/` and `tests/`; third-party dependency headers (Boost, fmt, spdlog, …) are excluded.
- **Report-only semantics**: `xmake tidy` exits 0 whether or not warnings are found. No `-k` gating, no baseline, no CI job this version.

## Capabilities

### New Capabilities

- `static-analysis`: the `xmake tidy` command — curated check set, project-code scoping, text/SARIF output, and report-only behavior.

### Modified Capabilities

(none)

## Impact

- New files: `.clang-tidy`, `plugins/tidy/xmake.lua`, `plugins/tidy/scan.py` (plus a bundled self-test fixture).
- No source changes; no new dependencies (clang-tidy already ships with the LLVM toolchain used everywhere).
- `CLAUDE.md`: document `xmake tidy` in the Build & Run section.
