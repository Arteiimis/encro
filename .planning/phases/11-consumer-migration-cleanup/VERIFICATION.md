# Phase 11 Verification: Consumer Migration & Cleanup

**Date verified:** 2026-04-30
**Verifier:** GSD execute-phase agent
**Status:** PASS

## Requirements Coverage

| Requirement | Description | Validated? | Evidence |
|-------------|-------------|------------|----------|
| MIG-01 | All 7 consumer files use OO API directly — no facade dependency | PASS | Zero `#include "pack/pack_facade.h"` in entire codebase; all consumers use PackService/Packer directly |
| MIG-02 | `pack_facade.h` and all `[[deprecated]]` wrappers removed | PASS | File deleted; `rg "pack_facade" src/ tests/` returns zero results |
| MIG-03 | 945 assertions pass, zero regressions | PASS | 945 assertions / 225 test cases / 0 failures |
| MIG-04 | Consumer diffs show only API migrations — no accidental logic changes | PASS | git diff confirms only include/call-site changes, zero logic modifications |
| MIG-05 | All 8 E2E CLI workflows produce identical output | DEFERRED | Requires test media + FFmpeg; integration tests pass (945 assertions) |
| MIG-06 | Final cleanup — include audit, dead code removal, using directive review | PASS | Zero facade includes; headers use pack_types.h; `using namespace pack::detail` in 2 consumer files (justified) |

## Decision Validation

| Decision | Expected Behavior | Observed Behavior | Status |
|----------|------------------|-------------------|--------|
| D-01: All-at-once migration | Single commit, all 7 consumers | All 7 migrated in one set of changes | PASS |
| D-02: Grouping→Packer, orchestration→PackService | Correct API surface used per consumer | video_output_planning uses Packer only; others use PackService (+ Packer for make_unique) | PASS |
| D-03: Per-call-site stack instances | Each function creates its own instances | Each consumer function creates stack-local Packer/PackService instances | PASS |
| D-04: Headers use pack_types.h | .h files only include pack_types.h | archive_plan.h and picture_process.h both use pack_types.h | PASS |
| D-05: Full E2E (8 CLI paths) | 8 paths verified | Deferred — requires test media environment | DEFERRED |

## Cross-Subsystem Checks

- `video_process.cpp` — packEncodedVideos still calls `buildGroupOrdinalRanges` (static), `appendOrdinalRangeSuffix` (static), `runPackPlan` (instance) correctly
- `picture_process.cpp` — compress and non-compress branches both use Packer/PackService stack instances correctly
- `pipeline.cpp` — runPackOnly creates PackService with unique_ptr<Packer>
- `video_output_planning.cpp` — both groupEncodedVideosForPack overloads use Packer stack instances
- `archive_plan.cpp` — static methods on PackService, zero instance management needed

## Include Audit

| File | Includes | Status |
|------|----------|--------|
| archive_plan.h | pack_types.h | Correct |
| archive_plan.cpp | pack_service.h | Correct |
| picture_process.h | pack_types.h | Correct |
| picture_process.cpp | pack_service.h, packer.h | Correct |
| pipeline.cpp | pack_service.h, packer.h | Correct (needed for make_unique<Packer>) |
| video_output_planning.cpp | packer.h | Correct |
| video_process.cpp | pack_service.h, packer.h | Correct (needed for make_unique<Packer>) |

## Deviations

- `pipeline.cpp` and `video_process.cpp` require `#include "pack/packer.h"` in addition to plan — `std::make_unique<pack::Packer>()` needs complete type, not just forward declaration from `pack_service.h`.
- E2E CLI verification (MIG-05) deferred — requires real test media and FFmpeg (environment-dependent).

## Environment

| Component | Version |
|-----------|---------|
| OS | Windows 11 |
| Compiler | clang-cl (C++26) |
| Build tool | xmake |
| Test framework | Catch2 |
| Assertions | 945 (225 test cases) |
