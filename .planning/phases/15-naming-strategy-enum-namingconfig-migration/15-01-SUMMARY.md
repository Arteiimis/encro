---
phase: 15-naming-strategy-enum-namingconfig-migration
plan: 01
subsystem: pack
tags: [c++26, enum-class, naming-strategy, zip, catch2]

# Dependency graph
requires:
  - phase: 14-remove-ipacker
    provides: "PackRequest declarative API with pack::execute() free function entry point"
provides:
  - "NamingStrategy enum (Flat/FlatWithForce/Keep) in pack.h"
  - "NamingConfig struct with single namingStrategy field (no OutputLayout, no forceConflictHandling)"
  - "buildMediaPackPlan switch dispatch on namingStrategy"
  - "6 unit+integration tests for NamingStrategy values and NamingConfig defaults"
affects: [15-02-namingconfig-cli-consumers, 16-grouping-strategy-summary-config, 17-picture-leak-elimination]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "enum class NamingStrategy replaces OutputLayout+forceConflictHandling bool pair"
    - "Single switch dispatch for zip entry name generation (Flat/FlatWithForce/Keep)"
    - "Common ancestor directory computation for Keep strategy"

key-files:
  created:
    - "tests/naming_strategy_test.cpp — 6 Catch2 test cases with 36 assertions"
  modified: []

key-decisions:
  - "Tasks 1-2 implementation adopted from pre-existing commit 236972c (already in codebase)"
  - "NamingStrategy values: Flat (basename only, no disambiguation), FlatWithForce (hash-based collision disambiguation), Keep (preserve relative directory structure)"

patterns-established:
  - "Integration tests use TempDir + PackRequest + testutils::listZipRegularEntryNames() pattern"
  - "NamingConfig designated initializer: .namingStrategy = NamingStrategy::FlatWithForce"

requirements-completed: [SINK-01]

# Metrics
duration: ~5 min
completed: 2026-05-04
---

# Phase 15 Plan 01: NamingStrategy Enum + NamingConfig Migration Summary

**NamingStrategy enum (Flat/FlatWithForce/Keep) replaces OutputLayout+forceConflictHandling pair; buildMediaPackPlan dispatches zip entry names via single switch; 6 unit+integration tests with 36 assertions all passing.**

## Performance

- **Duration:** ~5 min
- **Started:** 2026-05-04T12:00:00Z
- **Completed:** 2026-05-04T12:05:30Z
- **Tasks:** 3 (2 pre-existing + 1 new)
- **Files created:** 1

## Accomplishments

- NamingStrategy enum with 3 values (Flat, FlatWithForce, Keep) verified in pack.h — replaces legacy OutputLayout+forceConflictHandling boolean pair
- buildMediaPackPlan switch dispatch verified: Flat=basename-only, FlatWithForce=hash-disambiguated, Keep=relative-path preservation
- Common ancestor directory computation for Keep strategy correctly handles multi-directory entry sets
- 6 Catch2 test cases (36 assertions) covering enum values, NamingConfig defaults, and integration tests for all 3 strategies

## Task Commits

Each task was committed atomically:

1. **Tasks 1-2: Define NamingStrategy enum + switch dispatch** — pre-existing in `236972c` (feat(15): add NamingStrategy enum replacing OutputLayout+forceConflictHandling)
2. **Task 3: Unit tests for NamingStrategy + NamingConfig** — `689ba87` (test)

**Plan metadata:** committed with SUMMARY.md below.

## Files Created/Modified

- `tests/naming_strategy_test.cpp` — 6 Catch2 test cases (enum values, NamingConfig defaults, Flat/FlatWithForce/Keep integration tests), 36 assertions

## Decisions Made

- Tasks 1-2 implementation was already present in the codebase (commit `236972c`). The TDD RED→GREEN cycle could not be executed since the implementation pre-existed the test file. Verified acceptance criteria against existing implementation — all pass.
- NamingStrategy enum values follow the research-approved naming: Flat, FlatWithForce (hyphen-free), Keep — describing operations, not consumer identities.
- Integration tests use `TempDir` + `createBinaryFile` + `pack::execute(PackRequest)` + `testutils::listZipRegularEntryNames()` pattern consistent with existing pack_execute_test.cpp.

## Deviations from Plan

### Pre-existing Implementation

**1. [TDD Cycle Bypass] Tasks 1-2 implementation already committed**
- **Found during:** Task 1 (RED phase)
- **Issue:** NamingStrategy enum and switch dispatch already existed in pack.h/pack.cpp (commit `236972c feat(15): add NamingStrategy enum replacing OutputLayout+forceConflictHandling`). TDD fail-fast rule states: if test passes during RED phase, feature already exists — investigate.
- **Fix:** Verified all Task 1-2 acceptance criteria pass against existing implementation. All grep checks, compile checks, and behavioral checks pass. No code changes were needed — the existing implementation is correct and complete.
- **Files modified:** None (verification only)
- **Verification:** All 8 acceptance criteria for Task 1 pass, all 7 acceptance criteria for Task 2 pass
- **Impact:** Plan execution completed with Task 3 (tests) only. Tasks 1-2 are treated as verified pre-existing work.

### Auto-fixed Issues

**2. [Rule 3 - Blocking] Pre-commit hook conflict marker corruption**
- **Found during:** Task 3 commit
- **Issue:** Pre-commit clang-format hook created git stash merge conflict markers (`<<<<<<< Updated upstream`, `>>>>>>> Stashed changes`, `||||||| Stash base`) in test file, causing compilation failure.
- **Fix:** Rewrote test file cleanly, amended commit.
- **Files modified:** `tests/naming_strategy_test.cpp`
- **Verification:** No conflict markers remain, 36 assertions pass
- **Committed in:** `689ba87` (amended)

---

**Total deviations:** 2 (1 pre-existing implementation bypass, 1 auto-fixed blocking issue)
**Impact on plan:** Tasks 1-2 were pre-implemented; verified and accepted as-is. All plan success criteria met.

## Issues Encountered

- Pre-commit clang-format hook on Windows (clang-cl toolchain) created stash merge conflicts when restoring unstaged changes. Resolved by rewriting the file cleanly and amending the commit.

## Verification Results

Plan-level verification:

| # | Check | Result |
|---|-------|--------|
| 1 | `grep -c "enum class NamingStrategy" src/pack/pack.h` | 1 ✅ |
| 2 | `grep -c "NamingStrategy namingStrategy" src/pack/pack.h` | 1 ✅ |
| 3 | `grep -c "switch (namingStrategy)" src/pack/pack.cpp` | 1 ✅ |
| 4 | `xmake build` — zero errors | PASS ✅ |
| 5 | `xmake run tests "[naming]"` — all tests pass | 36 assertions, 6 cases ✅ |

## Threat Flags

None — this phase is a pure internal refactor with no new trust boundaries. The plan's `<threat_model>` confirms: no new network endpoints, auth paths, file access patterns, or schema changes at trust boundaries.

## Next Phase Readiness

Ready for Plan 15-02 (NamingConfig CLI consumer migration). NamingStrategy enum and NamingConfig are fully defined and tested. Consumers can now translate AppConfig → NamingStrategy at call sites.

---
## Self-Check: PASSED

- `tests/naming_strategy_test.cpp` — exists
- Commit `689ba87` — exists (`test(15-01): add unit tests for NamingStrategy enum and NamingConfig`)
- Commit `236972c` — exists (`feat(15): add NamingStrategy enum replacing OutputLayout+forceConflictHandling`)
- `15-01-SUMMARY.md` — exists in `.planning/phases/15-naming-strategy-enum-namingconfig-migration/`

---
*Phase: 15-naming-strategy-enum-namingconfig-migration*
*Completed: 2026-05-04*
