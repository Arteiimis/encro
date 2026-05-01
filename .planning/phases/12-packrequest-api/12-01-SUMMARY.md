---
phase: 12-packrequest-api
plan: 01
subsystem: api
tags: [packrequest, pack.h, designated-initializer, execute, c++26]

# Dependency graph
requires: []
provides:
  - pack.h public API header — single #include for pack module consumers (D-07)
  - PackRequest declarative type with designated initializer support
  - PackMode enum (Media/Directory)
  - NamingConfig sub-struct (layout + forceConflictHandling)
  - PackRunResult moved from pack_types.h to pack.h
  - execute() free function declaration in pack namespace
affects:
  - Phase 12 Plan 02 (execute() implementation)
  - Phase 12 Plan 03 (consumer migration)
  - Phase 12 Plan 04 (archive_plan deletion)
  - Phase 13 (grouping unification)
  - Phase 14 (IPacker removal)

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Designated initializers for PackRequest construction (C++20)"
    - "Free function API pattern: pack::execute() as namespace-level function"
    - "Forward-declared cross-module types: jobstate::Store, appctx::OutputLayout"
    - "Public/internal header split: pack.h (public) vs pack_types.h (internal)"

key-files:
  created:
    - src/pack/pack.h — Public API header (61 lines): PackRequest, PackMode, NamingConfig, PackRunResult, execute()
  modified:
    - src/pack/pack_types.h — PackRunResult struct removed (moved to pack.h), core/error_handle.h include removed
    - src/pack/pack_service.h — Added #include "pack/pack.h" for PackRunResult visibility

key-decisions:
  - "PackRunResult moved from pack_types.h to pack.h — public return type at API boundary"
  - "pack.h does NOT include any internal headers (pack_types.h, packer_types.h, pack_service.h, packer.h)"
  - "execute() declared as free function: auto execute(PackRequest const&) -> eh::Result<PackRunResult>"
  - "PackRequest uses std::optional for nullable fields (naming, maxParallelJobs) — no sentinel values"

patterns-established:
  - "Pattern 1: Single public header per module — consumers only #include \"pack/pack.h\""
  - "Pattern 2: Free function execute() as module entry point — internal lifecycle managed by module"

requirements-completed:
  - SIMPLIFY-01
  - SIMPLIFY-03
  - SIMPLIFY-04

# Metrics
duration: ~10min
completed: 2026-04-30
---

# Phase 12 Plan 01: pack.h Public API Header Summary

**PackRequest declarative type, PackMode enum, NamingConfig, and execute() declaration in single public header pack.h — PackRunResult moved from pack_types.h, zero behavior regressions across 945 assertions**

## Performance

- **Duration:** ~10 min
- **Started:** 2026-04-30T23:26:58Z
- **Completed:** 2026-04-30T23:36:08Z
- **Tasks:** 1 (TDD: RED + GREEN)
- **Files modified:** 3 (1 created, 2 modified)

## Accomplishments

- Created `src/pack/pack.h` — the single public header consumers `#include` per D-07
- Defined `PackRequest` struct with `std::optional` fields (entries, mode, outputDir, compact, naming, maxParallelJobs, recursive, jobState) — covers all 3 consumer paths (picture/video/pack-only)
- Defined `PackMode` enum (Media/Directory) per D-03
- Defined `NamingConfig` sub-struct (layout + forceConflictHandling) per D-04
- Moved `PackRunResult` from `pack_types.h` to `pack.h` — no ODR violation, single definition point
- Declared `pack::execute(PackRequest const&) -> eh::Result<PackRunResult>` free function per D-05
- Removed `#include "core/error_handle.h"` from `pack_types.h` (no longer needed after PackRunResult removal)
- All 945 assertions across 225 test cases pass — zero behavior regression

## Task Commits

TDD cycle: RED → GREEN (no refactor needed)

1. **RED: Standalone compile test** — `6d4a592` (test)
   - Created `tests/pack_api_standalone_compile_test.cpp`
   - Fails to compile because `pack/pack.h` doesn't exist yet — correct RED signal

2. **GREEN: Implementation** — `d75622e` (feat)
   - Created `src/pack/pack.h` with all public types and execute() declaration
   - Modified `src/pack/pack_types.h` — removed PackRunResult and error_handle.h include
   - Modified `src/pack/pack_service.h` — added `#include "pack/pack.h"` for PackRunResult

## Files Created/Modified

- `src/pack/pack.h` — Public API header: PackRunResult, PackMode, NamingConfig, PackRequest, execute() (61 lines)
- `src/pack/pack_types.h` — Removed PackRunResult struct (7 lines removed) and `#include "core/error_handle.h"`
- `src/pack/pack_service.h` — Added `#include "pack/pack.h"` for PackRunResult type visibility
- `tests/pack_api_standalone_compile_test.cpp` — Standalone compile test verifying pack.h isolation (44 lines)

## Verification

### Acceptance Criteria — All PASS

| # | Criterion | Result |
|---|-----------|--------|
| 1 | `struct PackRequest` in pack.h = 1 | PASS |
| 2 | `enum class PackMode` in pack.h = 1 | PASS |
| 3 | `struct NamingConfig` in pack.h = 1 | PASS |
| 4 | `auto execute` in pack.h = 1 | PASS |
| 5 | `struct PackRunResult` in pack.h = 1 | PASS |
| 6 | `struct PackRunResult` in pack_types.h = 0 | PASS |
| 7 | pack.h does NOT transitively include internal headers | PASS (proven by compilation) |
| 8 | `xmake build encro` exits 0 | PASS |
| 9 | `xmake build tests` exits 0 | PASS |
| 10 | All 945 assertions pass (225 test cases) | PASS |

### Plan-level Verification

```bash
xmake build encro 2>&1 | tail -1
# → build ok, spent 16.719s

xmake build tests 2>&1 | tail -1
# → build ok, spent 21.266s

xmake run tests 2>&1 | tail -1
# → All tests passed (945 assertions in 225 test cases)
```

## TDD Gate Compliance

| Gate | Commit | Status |
|------|--------|--------|
| RED | `6d4a592` — `test(12-01): add failing compile test for pack.h standalone API` | ✓ Test failed correctly (pack.h not found) |
| GREEN | `d75622e` — `feat(12-01): implement pack.h public API header with PackRequest types` | ✓ All builds and tests pass |
| REFACTOR | N/A | Not needed — implementation is minimal and clean |

## Decisions Made

- Used `fs = std::filesystem` namespace alias in pack.h (consistent with existing pack headers)
- Forward-declared `jobstate::Store` (pointer field, no full type needed) and `appctx::OutputLayout` (enum used by NamingConfig)
- Kept `pack_service.h` including `pack/pack.h` for PackRunResult — no circular dependency (pack.h does not include pack_service.h)

## Deviations from Plan

None — plan executed exactly as written.

## Issues Encountered

None — all steps completed without issues.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- `pack.h` is ready for Phase 12 Plan 02 (execute() implementation)
- `PackRunResult` is at the API boundary — consumers can use it via `#include "pack/pack.h"`
- All internal headers (pack_types.h, packer_types.h, pack_service.h) remain available for Phase 12 Plans 02-04 internal implementation
- No blockers

## Known Stubs

None — all types are fully defined. `execute()` is declared but not yet implemented (Phase 12 Plan 02).

## Threat Flags

None — no new network endpoints, auth paths, or file access patterns introduced. Header-only change with existing trust boundaries preserved.

---

*Phase: 12-packrequest-api*
*Completed: 2026-04-30*
