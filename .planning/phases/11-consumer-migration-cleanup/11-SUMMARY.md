# Phase 11 Execution Summary: Consumer Migration & Cleanup

**Date:** 2026-04-30
**Plan:** 11-01-migrate-consumers-remove-facade
**Status:** COMPLETE

## What Was Done

All 11 tasks executed — single plan, single wave.

| Task | Status | What |
|------|--------|------|
| T01 | Done | `archive_plan.h` include → `pack/pack_types.h` |
| T02 | Done | `archive_plan.cpp` — 3 static PackService calls |
| T03 | Done | `picture_process.h` include → `pack/pack_types.h` |
| T04 | Done | `picture_process.cpp` — 8 facade calls migrated (Packer+PackService) |
| T05 | Done | `pipeline.cpp` — 1 facade call → PackService stack instance |
| T06 | Done | `video_output_planning.cpp` — 2 grouping calls → Packer stack instances |
| T07 | Done | `video_process.cpp` — 3 facade calls migrated (PackService) |
| T08 | Done | `pack_facade.h` deleted from repository |
| T09 | Done | Include audit — zero facade includes, headers use pack_types.h, consumers include only what they use |
| T10 | Done | `xmake build` ok, 945 assertions / 225 test cases / 0 failures |
| T11 | Deferred | E2E CLI (8 paths) — requires real test media + FFmpeg |

## Files Modified (7)

- `src/core/archive_plan.h` — include swap
- `src/core/archive_plan.cpp` — static PackService methods
- `src/picture/picture_process.h` — include swap
- `src/picture/picture_process.cpp` — Packer+PackService stack instances
- `src/app/pipeline.cpp` — PackService stack instance (+ packer.h include)
- `src/video/video_output_planning.cpp` — Packer stack instances
- `src/video/video_process.cpp` — PackService static+instance (+ packer.h include)

## File Deleted (1)

- `src/pack/pack_facade.h` — 248 lines, 21 `[[deprecated]]` wrappers removed

## Deviations

- `pipeline.cpp` and `video_process.cpp` needed `#include "pack/packer.h"` in addition to `pack/pack_service.h` — `std::make_unique<pack::Packer>()` requires complete type.
- Task 11 (E2E CLI) deferred — environment-specific (requires test media + FFmpeg).
