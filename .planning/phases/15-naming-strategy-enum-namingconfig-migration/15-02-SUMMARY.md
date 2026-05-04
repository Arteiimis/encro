---
phase: 15-naming-strategy-enum-namingconfig-migration
plan: 02
subsystem: pack
tags: [c++26, naming-strategy, enum, refactor, migration]

# Dependency graph
requires:
  - phase: 15-01
    provides: NamingStrategy enum, NamingConfig struct with namingStrategy field
provides:
  - Directory mode zip entry name dispatch via single switch on NamingStrategy
  - AppConfig → NamingStrategy translation at pipeline call site
  - Full test suite green (3032 assertions) with zero behavioral regression
affects: [Phase 16 Grouping+Summary, Phase 17 Picture Leak Elim, Phase 18 PackPlan Internalize]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Single-switch enum dispatch for naming strategy (Flat/FlatWithForce/Keep)"
    - "toNamingStrategy() translation function isolates AppConfig→NamingStrategy mapping at consumer boundary"
    - "Default NamingStrategy::Flat when naming is nullopt (backward-compatible with old default)"

key-files:
  created: []
  modified:
    - src/pack/packer.h — buildDirectoryPackPlan signature: `bool forceNameConflictHandling` → `NamingStrategy namingStrategy = NamingStrategy::Flat`
    - src/pack/packer.cpp — buildDirectoryPackPlan implementation: ternary → switch(namingStrategy) with 3 cases
    - src/pack/pack.cpp — execute() Directory mode: `request.naming->namingStrategy` instead of `->forceConflictHandling`
    - src/app/pipeline.cpp — toNamingStrategy() translation function + NamingConfig uses `.namingStrategy = toNamingStrategy(ctx.config)`
    - tests/picture/picture_process_tests.cpp — `.layout = appctx::OutputLayout::Flat` → `.namingStrategy = pack::NamingStrategy::Flat`

key-decisions:
  - "Directory mode zip entry names dispatched via single `switch (namingStrategy)` with 3 cases — Flat/FlatWithForce byte-identical to old bool-based behavior"
  - "toNamingStrategy() maps {Keep}→Keep, {Flat,force=true}→FlatWithForce, {Flat,force=false}→Flat — exhaustive, no invalid states"
  - "No forceConflictHandling field remains in pack module (src/pack/) — fully migrated to NamingStrategy"

patterns-established:
  - "Single-switch enum dispatch for naming strategy (Flat/FlatWithForce/Keep)"
  - "toNamingStrategy() translation function isolates AppConfig→NamingStrategy mapping at consumer boundary"

requirements-completed: [SINK-01]

# Metrics
duration: 3 min
completed: 2026-05-04
---

# Phase 15 Plan 02: NamingStrategy Consumer Migration — Signature Update + Pipeline Translation + Test Fixes Summary

**Directory mode pack entry naming dispatched via single `switch (namingStrategy)` with AppConfig→NamingStrategy translation at consumer boundary; 3032 assertions pass with zero behavioral regression.**

## Performance

- **Duration:** 3 min (verification only — implementation pre-committed)
- **Started:** 2026-05-04T20:12:00+08:00
- **Completed:** 2026-05-04T20:15:00+08:00
- **Tasks:** 3
- **Files modified:** 5 (all pre-implemented)

## Accomplishments

- buildDirectoryPackPlan signature updated from `bool forceNameConflictHandling` to `NamingStrategy namingStrategy = NamingStrategy::Flat` with backward-compatible defaults
- Single-switch dispatch in Directory mode produces byte-identical zip entry names for Flat and FlatWithForce strategies
- Keep strategy correctly preserves directory structure via `filePath.lexically_relative(dirPath).generic_string()`
- toNamingStrategy() in pipeline.cpp isolates AppConfig→NamingStrategy mapping at the consumer call site
- pack.cpp execute() uses `request.naming->namingStrategy` (not `->forceConflictHandling`) — full migration complete
- All 3032 assertions pass across 243 test cases, including pack-only collision-safe default and disabled-mode tests

## Task Commits

All code changes were pre-implemented in a single combined commit alongside Plan 15-01:

1. **Task 1 (TDD): Update buildDirectoryPackPlan signature + implementation for NamingStrategy** — pre-implemented in `236972c`
2. **Task 2: Translate AppConfig → NamingStrategy in pipeline.cpp** — pre-implemented in `236972c`
3. **Task 3: Fix compilation errors in tests + run full test suite** — pre-implemented in `236972c`

**Plan metadata:** `[current]` (docs: complete plan — this SUMMARY.md commit)

## Files Created/Modified

- `src/pack/packer.h` — Signature: `NamingStrategy namingStrategy = NamingStrategy::Flat` replaces `bool forceNameConflictHandling = false`
- `src/pack/packer.cpp` — Implementation: `switch (namingStrategy)` with 3 cases (Flat → filename, FlatWithForce → hash-prefixed, Keep → relative path)
- `src/pack/pack.cpp` — Call site: `request.naming->namingStrategy` instead of `->forceConflictHandling`
- `src/app/pipeline.cpp` — Translation: `toNamingStrategy()` maps AppConfig fields to NamingStrategy; NamingConfig uses `.namingStrategy = toNamingStrategy(ctx.config)`
- `tests/picture/picture_process_tests.cpp` — Fix: `.namingStrategy = pack::NamingStrategy::Flat` replaces `.layout = appctx::OutputLayout::Flat`

## Verification Results

### Plan-Level Verification

| # | Criterion | Result |
|---|-----------|--------|
| 1 | `xmake build` — zero compilation errors | ✅ PASS |
| 2 | `xmake run tests` — all tests pass, 3032 assertions | ✅ PASS |
| 3 | `grep -rn "forceConflictHandling" src/pack/` — zero matches | ✅ PASS |
| 4 | `grep -rn ".layout = ctx.config" src/app/` — zero matches | ✅ PASS |
| 5 | `grep -rn "toNamingStrategy" src/app/pipeline.cpp` — present | ✅ PASS |

### Task-Level Acceptance Criteria

**Task 1 (TDD):**
- ✅ `packer.h` line 85: `NamingStrategy namingStrategy = NamingStrategy::Flat` (not `bool forceNameConflictHandling`)
- ✅ `packer.cpp` line 707: `NamingStrategy namingStrategy,` (not `bool forceNameConflictHandling,`)
- ✅ `packer.cpp` contains `switch (namingStrategy)` with three cases (Flat/FlatWithForce/Keep)
- ✅ Flat case: `filePath.filename().generic_string()` — byte-identical to old force=false
- ✅ FlatWithForce case: `buildConflictHandledPackEntryName(dirPath, filePath)` — byte-identical to old force=true
- ✅ Keep case: `filePath.lexically_relative(dirPath).generic_string()` — new correct behavior
- ✅ `pack.cpp` execute() uses `request.naming->namingStrategy` (not `->forceConflictHandling`)
- ✅ `xmake build` compiles with zero errors

**Task 2 (Pipeline Translation):**
- ✅ `toNamingStrategy()` function present in pipeline.cpp anonymous namespace
- ✅ Maps `OutputLayout::Keep` → `NamingStrategy::Keep`, `forceNameConflictHandling=true` → `FlatWithForce`, otherwise → `Flat`
- ✅ `runPackOnly` uses `.namingStrategy = toNamingStrategy(ctx.config)` (no `.layout =` or `.forceConflictHandling =`)
- ✅ No other function modified in pipeline.cpp
- ✅ `xmake build` compiles with zero errors

**Task 3 (Test Fixes + Suite):**
- ✅ `picture_process_tests.cpp` uses `.namingStrategy = pack::NamingStrategy::Flat` (not `.layout = ...OutputLayout::Flat`)
- ✅ No test file references removed NamingConfig fields (`.layout` or `.forceConflictHandling`)
- ✅ `xmake build` compiles with zero errors
- ✅ `xmake run tests` passes all tests — 3032 assertions, 0 failures
- ✅ `pack-only pipeline defaults to collision-safe file names` test passes (FlatWithForce path)
- ✅ `pack-only pipeline can disable collision-safe file names` test passes (Flat path)

## Decisions Made

- Pre-implemented code accepted as-is (commit `236972c`) — all 5 files match plan specifications exactly. No adjustments needed.
- The single-switch dispatch in buildDirectoryPackPlan exactly matches the plan's `mapStringFn` approach: Flat → filename, FlatWithForce → conflict-handled, Keep → relative path.

## Deviations from Plan

### Pre-Implementation Deviation

**1. [Rule 0 — Pre-Implementation] All code changes pre-committed in combined feat(15) commit**
- **Found during:** Execution start (all 3 tasks)
- **Issue:** The entire implementation for Plan 15-02 was pre-committed as part of commit `236972c feat(15): add NamingStrategy enum replacing OutputLayout+forceConflictHandling`. This single commit covered both Plan 15-01 (enum/struct definitions) and Plan 15-02 (consumer migration + test fixes).
- **Fix:** None needed — verified all changes match plan specifications. Ran full build and test suite to confirm correctness (3032 assertions pass).
- **Files modified:** src/pack/packer.h, src/pack/packer.cpp, src/pack/pack.cpp, src/app/pipeline.cpp, tests/picture/picture_process_tests.cpp — all in commit `236972c`
- **Verification:** xmake build (clean), xmake run tests (3032 assertions pass), all grep verifications pass
- **Impact:** No new code commits needed for this plan. TDD RED-GREEN-REFACTOR cycle was not followed (code was pre-committed as a unit). Only SUMMARY.md documentation commit required.

---

**Total deviations:** 1 pre-implementation (code already committed)
**Impact on plan:** Zero functional impact. All acceptance criteria verified and passing. Pre-implementation was correct and complete.

## TDD Gate Compliance

⚠️ **Task 1** (`tdd="true"`) could not follow the RED-GREEN-REFACTOR cycle because implementation was pre-committed in `236972c`.

| Gate | Status | Notes |
|------|--------|-------|
| RED | ⚠️ SKIPPED | Implementation pre-dates plan execution |
| GREEN | ⚠️ SKIPPED | Code already compiling and tests passing |
| REFACTOR | ⚠️ SKIPPED | Clean implementation verified |

The naming_strategy_test.cpp (added in Plan 15-01, commit `689ba87`) provides NamingStrategy enum tests. The pack-only pipeline tests (`pipeline_pack_only_tests.cpp`) verify FlatWithForce and Flat strategies in Directory mode.

## Issues Encountered

None — all verification passed on first attempt.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Plan 15-02 completes the SINK-01 requirement: NamingStrategy enum fully integrated into all pack module consumers and Directory mode dispatch
- Ready for Phase 16 (GroupingStrategy + SummaryConfig on PackRequest)
- All internal pack types use NamingStrategy; no remaining `forceConflictHandling` references in pack module
- Picture and video consumers already use NamingConfig.namingStrategy (via pack::execute())

---
*Phase: 15-naming-strategy-enum-namingconfig-migration*
*Completed: 2026-05-04*
