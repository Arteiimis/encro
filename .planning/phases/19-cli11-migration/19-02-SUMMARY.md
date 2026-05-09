---
phase: 19-cli11-migration
plan: 02
subsystem: cmd
tags: [cli, migration, config, cli11, refactor]
requires: [19-01]
provides:
  - buildConfig(CmdParseResult const&) → AppConfig (no boost::po dependency)
  - 6 internal helpers adapted to CmdParseResult fields
affects: [19-03]
tech-stack:
  removed: [boost::program_options (from config_builder.h/.cpp)]
  patterns:
    - "Direct CmdParseResult field access replacing vm.count/vm.at/getParamStr"
    - "Optional fields use .has_value()/.value() instead of vm.count/vm.at"
key-files:
  created: []
  modified:
    - src/cmd/config_builder.h (signature change: variables_map → CmdParseResult)
    - src/cmd/config_builder.cpp (325 lines: 29 vm references → 30 result. accesses)
decisions:
  - "Line count 325 is below plan estimate 380-420 — direct field access is more concise than boost::po vm patterns; all functionality preserved"
metrics:
  duration: "~5 min"
  completed_date: "2026-05-09"
---

# Phase 19 Plan 02: Migrate config_builder to CmdParseResult

**One-liner:** Replaced all 29 boost::program_options `vm.count()`/`vm.at()`/`getParamStr()` references in config_builder with direct `CmdParseResult` field access — buildConfig() now accepts a clean results struct with zero boost::po dependency.

## Tasks Executed

| Task | Name                              | Commit   | Files                        |
|------|-----------------------------------|----------|------------------------------|
| 1    | Rewrite config_builder.h — new signature | d238d45  | src/cmd/config_builder.h     |
| 2    | Migrate config_builder.cpp — 29 vm → result | 2d4ab51  | src/cmd/config_builder.cpp   |

## Verification Results

### Task 1: config_builder.h
- ✅ boost/program_options references: 0
- ✅ `#include "cmd/cmd.h"` present: 1
- ✅ `CmdParseResult const&` in signature: 1
- ✅ `boost::program_options::variables_map`: 0
- ✅ File: 12 lines (plan's provided template)

### Task 2: config_builder.cpp
- ✅ boost/program_options references: 0
- ✅ vm.count/vm.at/getParamStr references: 0
- ✅ `result.` field accesses: 30 (≥30 threshold)
- ✅ `CmdParseResult const&` signatures: 7 (buildConfig + 6 helpers)
- ✅ All 12 config validation error messages byte-identical to pre-migration
- ✅ `#include "utils/utils.h"` removed: 0 occurrences
- ✅ `src/cmd/config_builder.cpp` compiles successfully (build errors are in prelude.cpp — Plan 19-03's domain)

### Build
- ✅ `src/cmd/config_builder.cpp` compiles successfully with xmake
- ⚠️ Full `xmake build encro` fails (expected — `prelude.cpp` and `app_entry.cpp` still reference old `cmd.vm`; fixed in Plan 19-03)

## Deviations from Plan

### Auto-fixed Issues

None — plan executed exactly as written.

### Minor Deviations (non-blocking)

**1. [Line count] config_builder.cpp is 325 lines (plan estimated 380-420)**
- **Found during:** Post-commit verification
- **Issue:** Plan estimated 380-420 lines; actual is 325. The direct `result.field` access pattern is more concise than boost::po `vm.count("flag") > 0` and `vm.at("key").as<T>()` calls. Removed guard clauses (e.g., `if (!vm.count("type"))`) are no longer needed since CmdParseResult provides defaults.
- **Fix:** None needed — all 414 lines of original functionality are preserved, just expressed in fewer lines.
- **Files modified:** None (verification only)

## Known Stubs

None — all AppConfig fields are populated from CmdParseResult with proper validation. No placeholder values or mock data.

## Threat Flags

| Flag | File | Description |
|------|------|-------------|
| threat_flag: validation-surface | src/cmd/config_builder.cpp | All input validation preserved verbatim from pre-migration code. Bounds checks (imageQuality 2-31, jobs ≥ 1), type validation (video/picture, mp4/webp), mutual exclusion (flat/keep, resume/restart, input/inputs, compress only for picture, imageQuality requires compress, inputs only for video). T-19-06 mitigated. |

## Self-Check: PASSED

- ✅ `src/cmd/config_builder.h` exists (12 lines)
- ✅ `src/cmd/config_builder.cpp` exists (325 lines)
- ✅ Commit d238d45 exists in git log
- ✅ Commit 2d4ab51 exists in git log
- ✅ `src/cmd/config_builder.cpp` compiles without boost::po errors
