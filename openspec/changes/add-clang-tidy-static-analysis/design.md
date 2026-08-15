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

`plugins/tidy/scan.py` is a parallel Python driver: read `build/compile_commands.json`, collect `.cpp` translation units, run one `clang-tidy -p build <tu>` per TU across a `ThreadPoolExecutor` (default 16 workers, like include-cleaner), and aggregate results. Default mode prints text; `--sarif` uses `--export-fixes` to emit SARIF. A `--selftest` mode scans a bundled fixture that contains a known violation and a `#pragma once` header, asserting the violation is flagged and the pragma-once check is not — the smallest runnable check that fails if the check set or header-filter breaks.

*Alternative considered*: a single `clang-tidy` invocation over all files — loses per-TU parallelism and the aggregation/summary logic.

### 4. Report-only: no gate flag this version

The plugin exposes no `-k`/check mode. Exit code is 0 when warnings are found and non-zero only on hard errors (missing compile database, missing clang-tidy, or driver failure). Gating, a baseline, and CI wiring are deliberately deferred so the check set and thresholds can stabilize on real output first.

*Alternative considered*: shipping `-k` immediately — rejected because without a baseline it gates on historical debt rather than new regressions (see proposal/exploration notes).

### 5. Complexity thresholds start at 80 / 25

`readability-function-size.LineThreshold = 80` and `readability-function-cognitive-complexity.Threshold = 25`. These are provisional but empirically grounded: a probe across the largest files flagged only 7 functions over 80 lines and ~4 over cognitive 25. Nesting depth is intentionally not set (`NestingThreshold`) — cognitive complexity is the better signal and covers nesting without mis-flagging table-driven or switch-heavy code.

*Alternative considered*: enabling `NestingThreshold` alongside cognitive complexity — redundant, so rejected.

### 6. `clang-analyzer-optin.*` stays on for v1

The `clang-analyzer-*` wildcard enables the opt-in analyzer checks. These can be noisier than the core analyzer, but report-only mode tolerates noise, and the header-filter already excludes the system-header cases where optin checks mostly fire. They can be excluded later if the report proves noisy.

## Risks / Trade-offs

- **clang-tidy is slower than a normal compile** → parallelized per TU with a configurable `-j` (default 16), matching include-cleaner.
- **Version drift (local 22 vs future CI 19)** → moot this version (local only); the wildcard check groups used here are stable across both.
- **Thresholds may need tuning after the first full run** → they live in one config file and are trivially adjustable; the proposal commits to report-only precisely so tuning does not block anyone.
- **`clang-analyzer-optin.*` noise** → acceptable in report-only mode; documented as a follow-up exclusion if needed.
- **Windows path separators** → handled by the dual-separator header-filter and path normalization in the driver.

## Migration Plan

- No data or state migration; nothing is persisted.
- Rollback: delete `.clang-tidy`, `plugins/tidy/`, and the `CLAUDE.md` line. No behavioral effect on `encro` itself.
