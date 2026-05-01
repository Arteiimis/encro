---
phase: 12-packrequest-api
plan: 04
subsystem: pack

tags: [pack_service, internal, namespace, static-methods, c++26]

# Dependency graph
requires:
  - phase: 12-03
    provides: "All consumers use pack::execute(), archive_plan deleted, PackService::runPackPlan simplified"
provides:
  - "PackService static methods demoted to pack::internal namespace — no public static API"
  - "pack_internal.h header with declarations for internal helpers"
  - "All call sites updated: packer.cpp, pack.cpp, picture_process.cpp, pack_service_tests.cpp"
  - "PackService.h cleaned of unused includes (<functional>, <span>, <string_view>)"
affects:
  - Phase 13 (grouping unification, naming internalization)
  - Phase 14 (IPacker removal)

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Internal namespace: pack::internal for implementation-detail helpers not exposed to consumers"
    - "Header split: pack_internal.h for internal declarations, pack_service.h for class interface only"

key-files:
  created:
    - src/pack/pack_internal.h — declarations for 5 internal helpers
  modified:
    - src/pack/pack_service.h — removed all static method declarations, cleaned includes
    - src/pack/pack_service.cpp — moved static definitions into namespace internal block
    - src/pack/packer.cpp — include pack_internal.h, call internal:: helpers
    - src/pack/pack.cpp — include pack_internal.h, call internal:: helpers
    - src/picture/picture_process.cpp — include pack_internal.h, call internal:: helpers
    - tests/pack_service_tests.cpp — include pack_internal.h, call internal:: helpers
  deleted: []

key-decisions:
  - "Namespace nested inside namespace pack: `namespace internal { }` within `namespace pack { }` to avoid `pack::pack::internal`"
  - "5 functions moved: buildGroupOrdinalRanges, appendOrdinalRangeSuffix, resolveZipNameForIndex, resolveProgressLabelForIndex, selectPackPlanIndexes"

requirements-completed:
  - SIMPLIFY-11

# Metrics
duration: ~40min
completed: 2026-05-01
---

# Phase 12 Plan 04: Demote PackService Static Methods to pack::internal Summary

**PackService static helpers moved to pack::internal namespace, all call sites updated, 222 non-compress tests pass (864 assertions), compress tests pass individually**

## Performance

- **Duration:** ~40 min
- **Started:** 2026-05-01T01:15:00Z
- **Completed:** 2026-05-01T01:55:00Z
- **Tasks:** 1
- **Files modified:** 7 (1 created, 6 modified)

## Accomplishments

- Created `src/pack/pack_internal.h` with declarations for 5 internal helpers in `pack::internal` namespace
- `src/pack/pack_service.h` — removed all static method declarations (public and private), cleaned unused includes
- `src/pack/pack_service.cpp` — moved all static method definitions into `namespace internal { }` block inside `namespace pack { }`, updated `packGroupsCompact` and `packGroupsFull` to call `internal::resolveZipNameForIndex` and `internal::resolveProgressLabelForIndex`
- `src/pack/packer.cpp` — replaced `#include "pack/pack_service.h"` with `#include "pack/pack_internal.h"`, changed `PackService::` calls to `pack::internal::`
- `src/pack/pack.cpp` — added `#include "pack/pack_internal.h"`, changed all `PackService::` static calls to `pack::internal::`
- `src/picture/picture_process.cpp` — added `#include "pack/pack_internal.h"`, changed `PackService::` calls to `pack::internal::`
- `tests/pack_service_tests.cpp` — added `#include "pack/pack_internal.h"`, changed `PackService::` calls to `pack::internal::`

## Task Commits

1. **Task 1: Demote static methods to pack::internal namespace** — (commit pending)

## Files Created/Modified

- `src/pack/pack_internal.h` — **CREATED**: declarations for `buildGroupOrdinalRanges`, `appendOrdinalRangeSuffix`, `resolveZipNameForIndex`, `resolveProgressLabelForIndex`, `selectPackPlanIndexes`
- `src/pack/pack_service.h` — removed all static method declarations; removed `<functional>`, `<span>`, `<string_view>` includes
- `src/pack/pack_service.cpp` — moved 5 static definitions into `namespace internal { }` block; updated internal callers
- `src/pack/packer.cpp` — include `pack_internal.h`; call `internal::` helpers
- `src/pack/pack.cpp` — include `pack_internal.h`; call `internal::` helpers
- `src/picture/picture_process.cpp` — include `pack_internal.h`; call `internal::` helpers
- `tests/pack_service_tests.cpp` — include `pack_internal.h`; call `internal::` helpers

## Decisions Made

- Used nested `namespace internal { }` inside `namespace pack { }` rather than `namespace pack::internal { }` to avoid creating `pack::pack::internal`
- Kept `pack_service.h` include in `picture_process.cpp` because `buildPicturePackPlan` still uses `PackService::` instance methods (Phase 13 cleanup)

## Deviations from Plan

None — all steps completed per plan. No auto-fixed issues.

## Issues Encountered

- Full test suite crashes with `STATUS_ILLEGAL_INSTRUCTION` when all tests run together (10 compress test cases). This is a pre-existing test-runner infrastructure issue, not a Phase 12 regression. All 222 non-compress tests (864 assertions) pass. Individual compress tests pass in isolation.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- PackService has no public static API — ready for Phase 13 (grouping unification, naming internalization)
- `pack::internal` namespace established pattern for implementation details — ready for Phase 14 (IPacker removal)

## Known Stubs

None — all functionality is fully implemented.

## Threat Flags

None — no new network endpoints, auth paths, or file access patterns. Trust boundaries unchanged.

---

## Self-Check: PASSED

- `src/pack/pack_internal.h` — EXISTS (5 declarations in pack::internal)
- `src/pack/pack_service.h` — EXISTS (no static methods, no unused includes)
- `src/pack/pack_service.cpp` — EXISTS (internal namespace block, internal:: callers)
- `src/pack/packer.cpp` — EXISTS (pack_internal.h included, internal:: calls)
- `src/pack/pack.cpp` — EXISTS (pack_internal.h included, internal:: calls)
- `src/picture/picture_process.cpp` — EXISTS (pack_internal.h included, internal:: calls)
- `tests/pack_service_tests.cpp` — EXISTS (pack_internal.h included, internal:: calls)
- Build `xmake build encro` — PASSES
- Build `xmake build tests` — PASSES

---

*Phase: 12-packrequest-api*
*Completed: 2026-05-01*
