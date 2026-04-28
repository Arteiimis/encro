---
phase: 05-picture-refactor-validation
plan: 02
subsystem: picture
tags: [c++, tdd, lambda-refactoring, anonymous-namespace]

# Dependency graph
requires:
  - plan: 05-01
    provides: "toJpgEntryName as free function (no longer a lambda capture)"
provides:
  - "addCompressTask free function in anonymous namespace (6 individual typed parameters)"
  - "All 4 call sites pass captured variables explicitly"
affects: [05-03 (final validation gate)]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "TDD RED→GREEN cycle for internal-linkage function extraction (D-05/D-06)"
    - "Individual typed parameters per D-02 — 4 ref params + 2 value params, no context structs"

key-files:
  created: []
  modified:
    - "src/picture/picture_process.cpp — Added addCompressTask free function in anonymous namespace; removed [&] lambda variable; updated 2 call site loops to pass explicit parameters"
    - "tests/picture/picture_process_tests.cpp — Added dedup verification test (RED gate → real assertions)"

key-decisions:
  - "Parameter order: mutable references first (compressedSet, ec, compressTasks), then const ref (tempDir), then value params (picPath, entryName) — follows codebase convention"
  - "toJpgEntryName call within addCompressTask body unchanged (already reads as a function call after Plan 05-01)"
  - "Test validates 3 behaviors: new picPath adds task, duplicate picPath is no-op, output path ends with .jpg"

patterns-established:
  - "TDD RED gate in picture subsystem: REQUIRE(false) placeholder converted to real dedup assertions in GREEN phase"

requirements-completed: [REF-04]

# Metrics
duration: 8 min
completed: 2026-04-27
---

# Phase 5 Plan 2: TDD Extract addCompressTask to Free Function Summary

**Extracted the addCompressTask `[&]` lambda to a named free function in the anonymous namespace using TDD RED→GREEN cycle. All 4 captured variables converted to individual typed parameters per D-02. New verification test validates dedup behavior and CompressTask creation.**

## Performance

- **Duration:** 8 min
- **Started:** 2026-04-27
- **Completed:** 2026-04-27
- **Tasks:** 2 (TDD: RED → GREEN)
- **Files modified:** 2

## Accomplishments

- Added `REQUIRE(false)` RED gate in `picture_process_tests.cpp` — confirmed failing before extraction
- Extracted `addCompressTask` to free function in anonymous namespace with 6 individual typed parameters:
  - `compressedSet` (mutable ref), `tempDir` (const ref), `ec` (mutable ref), `compressTasks` (mutable ref), `picPath` (value), `entryName` (value)
- Updated both call sites in `runPicturePackWorkflow` loop bodies to pass captured variables explicitly
- Converted RED gate to real assertions: verifies dedup (same picPath → no new task), multi-file (different picPath → second task), and .jpg suffix propagation
- All existing tests pass — no regression, no behavioral changes

## Task Commits

1. **Task 1 (RED): test(05-02)** — `aeea706` — Add failing REQUIRE(false) gate for addCompressTask extraction
2. **Task 2 (GREEN): feat(05-02)** — `4c553d9` — Extract addCompressTask to named function; update call sites; convert RED gate to real assertions

## Files Created/Modified

- `src/picture/picture_process.cpp` — Added `addCompressTask` free function (+16 lines in anonymous namespace); removed `auto addCompressTask = [&](...) { ... };` lambda variable (-15 lines); updated 2 for-loop call sites to pass explicit parameters (summaryPics loop: +5 params; scannedPics loop: +5 params)
- `tests/picture/picture_process_tests.cpp` — Added TEST_CASE "addCompressTask deduplicates and creates valid CompressTask entries" (+38 lines); 3 behaviors tested: dedup, multi-file, .jpg suffix

## Decisions Made

None — followed plan as specified. All extraction patterns follow Phase 3/4 conventions (D-01, D-02, D-05).

## Deviations from Plan

None — plan executed exactly as written.

## Issues Encountered

None.

## Next Phase Readiness

- REF-04 fully satisfied — both `toJpgEntryName` and `addCompressTask` are now free functions in the anonymous namespace
- `picture_process.cpp` compress branch delegates to named functions instead of inline lambdas
- Ready for Plan 05-03: Final validation gate (full test suite + codebase audit)

---

## Self-Check: PASSED

- `src/picture/picture_process.cpp` — `addCompressTask` free function exists ✓
- `src/picture/picture_process.cpp` — No `auto addCompressTask = [&]` lambda ✓
- Both call sites pass explicit parameters ✓
- `tests/picture/picture_process_tests.cpp` — No `REQUIRE(false)` ✓
- Verification test validates dedup and .jpg suffix ✓
- All tests pass ✓

---

*Phase: 05-picture-refactor-validation*
*Completed: 2026-04-27*
