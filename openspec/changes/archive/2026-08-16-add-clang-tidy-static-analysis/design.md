# Design: Clang-Tidy Static Analysis

## Context

- The repo already drives clang-based tooling through xmake plugins under `plugins/<name>/{xmake.lua, scan.py}`: `format` (clang-format) and `include-cleaner` (clang-include-cleaner via a parallel Python driver). `include-cleaner` is the closest analog — same shape this change copies.
- `build/compile_commands.json` is auto-generated (`plugin.compile_commands.autoupdate`), so clang-tidy has a compile database with no extra build step.
- Local toolchain is clang-cl (Windows); the compile database therefore carries clang-cl/MSVC-style flags. Verified: clang-tidy 22.1.8 consumes it directly with `-p build` (no `--driver-mode` needed).
- clang-tidy is already on PATH (ships with the LLVM install used for clang-cl/clang-format).

## Goals / Non-Goals

**Goals:**
- One curated check set in a single repo-root config file.
- A `xmake tidy` command that scans all translation units and reports warnings scoped to `src/`/`tests/` only.
- Text output for humans, SARIF output for agents/tooling.

**Non-Goals:**
- CI job, `-k` gating, baseline management (deferred to a later version).
- Auto-fix (`--fix`); this is report-only.
- Convention machine-ification (identifier-naming, modernize-*); out of scope.

## Decisions

### 1. Allowlist `.clang-tidy` config (positive check list)

The config lists only the checks to run. Irrelevant ecosystems (`google-*`, `fuchsia-*`, `abseil-*`, `objc-*`, `llvmlibc-*`, `misc-include-cleaner`) are off by construction because they are never listed — no denylist needed. Only three in-group exclusions remain: `bugprone-easily-swappable-parameters` (opinionated, high false-positive rate), `performance-enum-size` (noise), and `portability-avoid-pragma-once` (contradicts the project's deliberate `#pragma once` convention).

```yaml
Checks: >
  clang-analyzer-*,
  bugprone-*,
  performance-*,
  portability-*,
  readability-function-size,
  readability-function-cognitive-complexity,
  -bugprone-easily-swappable-parameters,
  -performance-enum-size,
  -portability-avoid-pragma-once

CheckOptions:
  - key: readability-function-size.LineThreshold
    value: '80'
  - key: readability-function-cognitive-complexity.Threshold
    value: '25'
```

*Alternative considered*: `--checks='*'` with a large denylist — noisier and version-fragile; the allowlist is self-documenting and stable across clang-tidy versions.

### 2. header-filter uses a dual-separator regex

Warnings must be scoped to project code. The naive `^(src|tests)/` silently misses project headers on Windows (paths use backslashes) — verified during exploration. The filter SHALL be `(^|[\\/])(src|tests)[\\/]`, which matches both separators and the absolute-prefix case.

*Alternative considered*: none — this is the correctness fix, not a preference.

### 3. Driver mirrors include-cleaner

`plugins/tidy/scan.py` is a parallel Python driver: read `build/compile_commands.json`, deduplicate `.cpp` translation units (the compile database contains duplicate entries), run one `clang-tidy -p build <tu>` per TU across a `ThreadPoolExecutor`, and aggregate results. Default mode prints text; `--sarif` builds a SARIF 2.1.0 document from the parsed diagnostics and writes `build/tidy-results.sarif` (clang-tidy 22 has no native SARIF output — `--export-fixes` writes YAML only — so the driver parses the text output instead). Warnings are post-filtered to `src/` and `tests/` paths in the driver: `clang-analyzer-*` and `clang-diagnostic-*` diagnostics can bypass `-header-filter` and leak from third-party headers, so the driver enforces the scoping guarantee itself. Crashed TUs (clang-tidy can die with an access violation under memory pressure) are retried once sequentially after the parallel phase. Exit 1 (deterministic clang-tidy errors such as compile failures) is not retried; timeouts are reported and skipped.

SARIF `artifactLocation.uri` values are repo-root-relative (`src/...`); consumers (agents, future GitHub upload) resolve them against the repo root, not the SARIF file's own `build/` location — a deliberate deviation from the SARIF relative-base rule so the same file works for the deferred CI version. The `-f` filter matches the TU *path* (diverging from include-cleaner's content filter), because for a linter "scan this module's files" is the useful semantic.

*Alternative considered*: a single `clang-tidy` invocation over all files — loses per-TU parallelism and the aggregation/summary logic.

### 4. Report-only: no gate flag this version

The plugin exposes no `-k`/check mode. Exit code is 0 when warnings are found and non-zero only on hard errors (missing compile database, missing clang-tidy, or driver failure). Gating, a baseline, and CI wiring are deliberately deferred so the check set and thresholds can stabilize on real output first.

*Alternative considered*: shipping `-k` immediately — rejected because without a baseline it gates on historical debt rather than new regressions (see proposal/exploration notes).

### 5. Complexity thresholds start at 80 / 25

`readability-function-size.LineThreshold = 80` and `readability-function-cognitive-complexity.Threshold = 25`. These are provisional but empirically grounded: a probe across the largest files flagged only 7 functions over 80 lines and ~4 over cognitive 25. Nesting depth is intentionally not set (`NestingThreshold`) — cognitive complexity is the better signal and covers nesting without mis-flagging table-driven or switch-heavy code.

*Alternative considered*: enabling `NestingThreshold` alongside cognitive complexity — redundant, so rejected.

### 6. The semantic analyzer is opt-in (`--analyzer`)

`clang-analyzer-*` is the cost driver: a path-sensitive pass costing minutes and ~2GB peak per TU. A full parallel run on a 16-core/32GB machine saturated CPU, exhausted memory, and crashed clang-tidy processes. The default `xmake tidy` therefore strips the analyzer with `--checks=-clang-analyzer-*` (verified additive semantics: it removes analyzer checks from the config-derived set without touching the rest), yielding a ~2.5-minute fast scan at low memory. `xmake tidy --analyzer` runs the full `.clang-tidy` set at a capped default parallelism (`-j 4` vs fast-mode `-j 8`). This mirrors how analyzer runs are conventionally treated (scheduled/occasional deep scans, not per-edit linting).

*Alternative considered*: analyzer always on with capped node budgets — still minutes per TU, so the default lint path stays heavy; rejected.

## Risks / Trade-offs

- **clang-tidy is slower than a normal compile** → parallelized per TU with a configurable `-j`; the analyzer dominates cost and is opt-in (Decision 6), bringing the default full scan to ~2.5 minutes.
- **Analyzer runs can exhaust memory under high parallelism** (minutes + ~2GB per TU) → `--analyzer` caps the default at `-j 4`, and crashed TUs are retried sequentially.
- **Version drift (local 22 vs future CI 19)** → moot this version (local only); the wildcard check groups used here are stable across both.
- **Thresholds may need tuning after the first full run** → they live in one config file and are trivially adjustable; the proposal commits to report-only precisely so tuning does not block anyone.
- **`clang-analyzer-*` diagnostics can bypass `-header-filter`** → the driver post-filters warnings to `src/`/`tests/` paths, guaranteeing the spec's scoping requirement regardless of clang-tidy behavior.
- **Windows path separators** → handled by the dual-separator header-filter and path normalization in the driver.

## Migration Plan

- No data or state migration; nothing is persisted.
- Rollback: delete `.clang-tidy`, `plugins/tidy/`, and the `CLAUDE.md` line. No behavioral effect on `encro` itself.
