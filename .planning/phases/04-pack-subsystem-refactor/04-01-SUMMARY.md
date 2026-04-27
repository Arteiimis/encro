---
phase: 04-pack-subsystem-refactor
plan: 01
subsystem: pack
tags: [c++, lambda-refactoring, anonymous-namespace, std-function, factory-pattern]

# Dependency graph
requires:
  - phase: 03-video-subsystem-refactor
    provides: "Refactoring patterns D-01 (anonymous namespace free functions), D-02 (individual typed parameters), D-05 (camelCase naming), TDD approach for internal-linkage functions"
provides:
  - "makeSubsetZipNameResolver factory function in anonymous namespace"
  - "makeSubsetProgressLabelResolver factory function in anonymous namespace"
  - "REF-02: selectPackPlanIndexes no longer contains lambda-wrapping-lambda"
affects: [04-02 (parallel plan, same pack/ subsystem)]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Factory function returning std::function<std::string(std::size_t)> for index-remapping resolvers"
    - "Anonymous namespace extraction following Phase 3 D-01/D-02/D-05 conventions"
    - "TDD RED→GREEN commit sequence for refactoring with internal-linkage functions"

key-files:
  created: []
  modified:
    - "src/pack/pack_service.cpp — Added makeSubsetZipNameResolver and makeSubsetProgressLabelResolver in anonymous namespace; replaced inline lambdas in selectPackPlanIndexes with named function calls"
    - "tests/pack_service_tests.cpp — Added TEST_CASE verifying index remapping correctness through selectPackPlanIndexes result"

key-decisions:
  - "Extracted each lambda-wrapping-lambda as a named factory function (makeSubsetZipNameResolver, makeSubsetProgressLabelResolver) taking originalResolver and selectedIndexes as individual typed parameters — follows Phase 3 pattern D-02"
  - "Used std::shared_ptr<std::vector<std::size_t>> for selectedIndexes parameter to match existing allocation pattern in selectPackPlanIndexes"
  - "ProgressLabelResolver returns empty std::function when input is null (nil check before constructing lambda) — preserves existing nil-guard behavior"

patterns-established:
  - "Factory function pattern for std::function resolvers: auto makeX() -> std::function<ReturnType(Param)> returning a capturing lambda"

requirements-completed: [REF-02]

# Metrics
duration: 4 min
completed: 2026-04-27
---

# Phase 4 Plan 1: Extract Lambda-Wrapping-Lambda from selectPackPlanIndexes Summary

**Eliminated lambda-wrapping-lambda in selectPackPlanIndexes by extracting two named factory functions (makeSubsetZipNameResolver, makeSubsetProgressLabelResolver) into the anonymous namespace, satisfying REF-02.**

## Performance

- **Duration:** 4 min
- **Started:** 2026-04-27T10:50:13Z
- **Completed:** 2026-04-27T10:54:14Z
- **Tasks:** 1 (TDD: RED → GREEN)
- **Files modified:** 2

## Accomplishments

- Extracted `makeSubsetZipNameResolver` — factory function returning a `std::function<std::string(std::size_t)>` that remaps a subset index to the original plan's zip name resolver (with nil-guard fallback to `defaultZipNameForIndex`)
- Extracted `makeSubsetProgressLabelResolver` — factory function returning a `std::function<std::string(std::size_t)>` that remaps a subset index to the original plan's progress label resolver (with nil-guard returning empty function)
- `selectPackPlanIndexes` now calls `makeSubsetZipNameResolver(plan.zipNameForIndex, selectedIndexes)` and `makeSubsetProgressLabelResolver(plan.progressLabelForIndex, selectedIndexes)` instead of inline lambda-wrapping-lambda
- New test case verifies correct index remapping: `selected[0]=1` → `zipNameForIndex(0)` returns `"arch1.zip"` and `progressLabelForIndex(0)` returns `"Zipping archive 1"`

## Task Commits

Each task was committed atomically:

1. **Task 1 (RED): Add failing test gate** — `c074a01` (test)
2. **Task 1 (GREEN): Extract named helpers, wire call sites** — `4860a9f` (feat)

_Note: REFACTOR phase skipped per plan — extraction is minimal (each function 4-8 lines), clang-format applied during pre-commit hook._

## Files Created/Modified

- `src/pack/pack_service.cpp` — Added `makeSubsetZipNameResolver` and `makeSubsetProgressLabelResolver` in anonymous namespace (lines 47-66); replaced inline lambda-wrapping-lambda at `selectPackPlanIndexes` return statement (lines 150-152) with named function calls
- `tests/pack_service_tests.cpp` — Added TEST_CASE "selectPackPlanIndexes delegates to named helpers instead of lambda-wrapping-lambda" (lines 132-162) with CHECK assertions verifying zip name remapping, progress label remapping, and compact flag preservation

## Decisions Made

None — followed plan as specified. All extraction patterns (anonymous namespace placement, individual typed parameters, camelCase naming, trailing return type) follow Phase 3 conventions D-01, D-02, D-05.

## Deviations from Plan

None — plan executed exactly as written.

### Pre-existing Issue Noted

The test `groupPreparedEntries delegates packSourceEntries to named function` in `tests/packer_tests.cpp` still contains a `REQUIRE(false)` RED gate from Phase 3 that was never cleaned up. This causes 1 of 213 test cases to fail (894 of 895 assertions pass). This is out of scope for Plan 04-01 — no pack_service.cpp tests affected, no behavioral regression.

## Issues Encountered

- Pre-commit hook clang-format caused a merge conflict during the RED phase commit. Resolved by accepting the formatted version (`git checkout --theirs`) and re-committing.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- Plan 04-01 complete — REF-02 satisfied
- Ready for Plan 04-02 (extract `fillArchiveGroups` lambda-wrapping-lambda in packer.cpp) — runs in parallel, no file overlap with 04-01
- Pre-existing packer_tests.cpp RED gate failure should be resolved in Plan 04-02 (same test file, related scope)
- All pack_service_tests pass; 894 of 895 total assertions pass (single pre-existing failure in packer_tests.cpp)

---
## Self-Check: PASSED

- SUMMARY.md exists: True
- RED commit `c074a01` exists in git history
- GREEN commit `4860a9f` exists in git history
- All acceptance criteria verified via grep/documentation checks

---
*Phase: 04-pack-subsystem-refactor*
*Completed: 2026-04-27*
