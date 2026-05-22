---
phase: 02-file-management-observability
plan: 04
subsystem: logging
tags: [scoped-timer, pipeline-timing, observability]
requires: [02-02]
provides: [pipeline-stage-timing]
affects: [video-process, picture-process, pack-service]
tech-stack:
  added: []
  patterns: [RAII ScopedTimer, immediately-invoked lambda wrappers]
key-files:
  created: []
  modified:
    - src/video/video_process.cpp
    - src/picture/picture_process.cpp
    - src/pack/pack_service.cpp
decisions:
  - D-19: ScopedTimer placed at video.scan, video.encode, video.pack, picture.scan, picture.compress, picture.pack, pack.execute boundaries
  - D-20: ScopedTimer placed at pipeline orchestration function entry points in video_process.cpp, picture_process.cpp, pack_service.cpp
  - Probe stage bundled with scan -- no separate probe timer per plan instruction
  - Lambda wrappers used for mid-function ScopedTimer placement to control scope without restructuring code

metrics:
  duration: 3m
  completed_date: 2026-05-23
---

# Phase 2 Plan 4: Pipeline Stage Timing with ScopedTimer -- Summary

Instrumented the video, picture, and pack pipelines with RAII `logging::ScopedTimer` instances at all major stage boundaries. Each pipeline stage now automatically logs entry and elapsed time without manual timing code.

## Completed Tasks

### Task 1: Instrument video pipeline stages with ScopedTimer
**Commit:** `42979bf`

Added four ScopedTimer placements in `src/video/video_process.cpp`:
- `logging::ScopedTimer timer("video.scan")` in `scanInputVideos()` (line 167) -- covers both scan and probe since they are interleaved
- `logging::ScopedTimer timer("video.scan")` in `scanInputVideosFromFiles()` (line 185) -- same stage name, different function for multi-file input
- `logging::ScopedTimer timer("video.encode")` in `runScannedEncodingWorkflow()` -- wrapped around the `videobatch::runEncodingTasks()` call using a scoped block with `EncodeResultsMap` pre-declared outside the scope
- `logging::ScopedTimer timer("video.pack")` in `packEncodedVideos()` (line 391) -- covers the entire pack operation from plan building through pack::execute()

All existing code paths, control flow, and return values are unchanged. The `vidsRunRes` variable changed from `auto const` to `auto` to allow assignment inside the scoped block, but its usage after the block is identical.

### Task 2: Instrument picture and pack pipeline stages with ScopedTimer
**Commit:** `8b24c6b`

Added six ScopedTimer placements across `src/picture/picture_process.cpp` and `src/pack/pack_service.cpp`:

**picture_process.cpp:**
- `logging::ScopedTimer timer("picture.scan")` in `executeCompressPackWorkflow()` -- wrapped around `readAllPics()` via immediately-invoked lambda
- `logging::ScopedTimer timer("picture.compress")` in `executeCompressPackWorkflow()` -- wrapped around `compressImageBatch()` via immediately-invoked lambda
- `logging::ScopedTimer timer("picture.pack")` in `executeCompressPackWorkflow()` -- wrapped around `pack::execute()` via immediately-invoked lambda
- `logging::ScopedTimer timer("picture.scan")` in `executeDirectPackWorkflow()` -- wrapped around `readAllPics()` via immediately-invoked lambda
- `logging::ScopedTimer timer("picture.pack")` in `executeDirectPackWorkflow()` -- wrapped around `pack::execute()` via immediately-invoked lambda

**pack_service.cpp:**
- `logging::ScopedTimer timer("pack.execute")` in `PackService::packGroups()` (line 509) -- covers the entire pack operation (both compact and full modes)

Lambda wrappers use `[&]` capture and are immediately invoked -- references never outlive captured variables. ScopedTimer is stack-allocated, constructed in the lambda (logs "begin"), the function runs, the lambda returns, ScopedTimer destructor fires (logs "completed in Xms").

### Task 3: End-to-end verification -- run existing pipeline tests
**No commit (verification only)**

All existing pipeline test suites pass without regression:
- `[video-process]`: 33 test cases, 108 assertions -- PASSED
- `[picture-process]`: 15 test cases, 2135 assertions -- PASSED
- `[pack-service]`: 21 test cases, 78 assertions -- PASSED

Timing brackets confirmed in test output:
- Video: `video.scan begin/completed`, `video.encode begin/completed`, `video.pack begin/completed`
- Picture: `picture.scan begin/completed`, `picture.compress begin/completed`, `picture.pack begin/completed`
- Pack: `pack.execute begin/completed`
- Nested correctly: `video.pack` contains `pack.execute` -- inner completes before outer

## Deviations from Plan

None -- plan executed exactly as written.

## Key Decisions

1. **Mid-function ScopedTimer via scoped blocks:** For `runScannedEncodingWorkflow()`, used a `{ }` scoped block with `vidsRunRes` pre-declared outside. This keeps the timer's scope tight to the encode call without restructuring the function.
2. **Lambda wrappers for picture pipeline:** Used immediately-invoked `[&]` lambdas for picture scan, compress, and pack stages. This is the cleanest minimal-diff approach for mid-function ScopedTimer placement.
3. **PackService::packGroups() as single entry point:** Placed `pack.execute` timer in `packGroups()` rather than `runPackPlan()` because `packGroups()` is the single dispatch point for both compact and full modes -- one timer covers all pack operations regardless of mode.

## Known Stubs

None.

## Self-Check

- [x] `src/video/video_process.cpp` contains 4 ScopedTimer declarations (scan x2, encode, pack)
- [x] `src/picture/picture_process.cpp` contains 5 ScopedTimer declarations (scan x2, compress, pack x2)
- [x] `src/pack/pack_service.cpp` contains 1 ScopedTimer declaration (pack.execute)
- [x] All pipeline tests pass ([video-process], [picture-process], [pack-service])
- [x] `xmake build encro` compiles and links successfully
- [x] Commits `42979bf` and `8b24c6b` verified in git log
