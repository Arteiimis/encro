---
phase: 9
plan: "09-04"
subsystem: pack
tags: [facade, backward-compat, deprecated]
key-files:
  created:
    - src/pack/pack_facade.h
  modified:
    - src/video/video_process.cpp
    - src/picture/picture_process.h
    - src/picture/picture_process.cpp
    - src/video/video_output_planning.cpp
    - src/app/pipeline.cpp
    - src/core/archive_plan.h
    - src/core/archive_plan.cpp
---

# Summary: Plan 09-04 — Facade Layer + Consumer Migration

## What Was Built

Created `pack_facade.h` with 21+ `[[deprecated]]` inline static wrapper functions that forward to Packer and PackService. Each wrapper instantiates `static pack::Packer` + `static pack::PackService` internally for zero lifecycle management. All 6 consumer files swapped to `#include "pack/pack_facade.h"` and updated calls to `pack_facade::functionName(...)`. Facade is temporary — removed in Phase 11.

## Task Results

| Task | Description | Status |
|------|-------------|--------|
| 09-04-01 | Create pack_facade.h with 21+ `[[deprecated]]` static wrappers | Complete |
| 09-04-02 | Swap consumer includes to pack_facade.h, update calls | Complete |
| 09-04-03 | Verify test includes (keep direct Packer/PackService includes) | Complete |
| 09-04-04 | Full build + 909 assertion verification | Complete |

## Verification

- Build: `xmake build` — zero errors
- Tests: `xmake run tests` — 909 assertions passed in 215 test cases
- 6 consumer files use `pack_facade::` prefix for pack function calls
- `[[deprecated]]` warnings expected from facade usage (not errors in C++26/clang-cl)

## Deviations

None.

## Self-Check: PASSED
