---
phase: 05-picture-refactor-validation
plan: 01
subsystem: picture
tags: [c++, lambda-refactoring, anonymous-namespace, static-function]

# Dependency graph
requires:
  - phase: 04-pack-subsystem-refactor
    provides: "D-01 (anonymous namespace), D-02 (individual typed parameters), D-05 (camelCase naming)"
provides:
  - "toJpgEntryName static free function in anonymous namespace"
  - "addCompressTask lambda capture list reduced (toJpgEntryName no longer a [&] capture)"
affects: [05-02 (addCompressTask extraction)]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Static free function extraction for zero-capture pure lambdas per D-03/D-06"

key-files:
  created: []
  modified:
    - "src/picture/picture_process.cpp — Added toJpgEntryName free function in anonymous namespace; removed lambda variable; all 3 call sites unchanged (syntax identical)"

key-decisions:
  - "Extracted as static free function (not a lambda variable) — toJpgEntryName has zero captures per D-03/D-06"
  - "Function signature uses trailing return type per convention: auto toJpgEntryName(std::string const& entryName) -> std::string"
  - "No TDD RED gate needed — D-06 classifies this as simple pure function extraction"

patterns-established:
  - "Zero-capture lambda → static free function extraction (trivial, no test-gate needed)"

requirements-completed: [REF-04]

# Metrics
duration: 2 min
completed: 2026-04-27
---

# Phase 5 Plan 1: Extract toJpgEntryName to Static Free Function Summary

**Extracted the toJpgEntryName lambda variable (lines 322-326) to a static free function in the anonymous namespace of picture_process.cpp. All 3 call sites remain syntactically identical — `toJpgEntryName(...)` works unchanged as a direct function call.**

## Performance

- **Duration:** 2 min
- **Started:** 2026-04-27
- **Completed:** 2026-04-27
- **Tasks:** 1
- **Files modified:** 1

## Accomplishments

- Extracted `toJpgEntryName` — a pure function with zero captures that was previously a `[](std::string const&) -> std::string` lambda variable — to a static free function in the anonymous namespace
- Function body is byte-identical to the original lambda (copy-paste extraction — `.rfind`, `substr`, `+ ".jpg"` logic unchanged)
- All 3 call sites in `runPicturePackWorkflow` use identical syntax (`toJpgEntryName(...)` already read as a function call)
- `addCompressTask` lambda's `[&]` capture list automatically stopped capturing `toJpgEntryName` (no longer a local variable)

## Task Commits

1. **Task 1: feat(05-01)** — `fbe3c50` — Extract toJpgEntryName lambda to static free function; remove lambda variable; build verification passes

## Files Created/Modified

- `src/picture/picture_process.cpp` — Added `toJpgEntryName` free function in anonymous namespace (line ~61); removed `auto toJpgEntryName = [](...) { ... };` lambda variable (-5 lines); 3 call sites unchanged

## Decisions Made

None — followed plan as specified. Zero-capture pure function extraction per D-03/D-06 requires no architectural decisions.

## Deviations from Plan

None — plan executed exactly as written.

## Issues Encountered

None.

## Next Phase Readiness

- REF-04 partially satisfied — toJpgEntryName no longer a lambda variable
- Ready for Plan 05-02: TDD extract `addCompressTask` to free function with explicit typed parameters
- `addCompressTask` lambda capture list is now cleaner (toJpgEntryName removed from captures)

---

## Self-Check: PASSED

- `src/picture/picture_process.cpp` — `toJpgEntryName` free function exists in anonymous namespace ✓
- `src/picture/picture_process.cpp` — No `auto toJpgEntryName = [` lambda variable ✓
- 3 call sites use identical `toJpgEntryName(...)` syntax ✓
- Build passes ✓

---

*Phase: 05-picture-refactor-validation*
*Completed: 2026-04-27*
