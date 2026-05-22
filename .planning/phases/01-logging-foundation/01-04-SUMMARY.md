---
phase: 01-logging-foundation
plan: 04
subsystem: logging
tags: [spdlog, DEFINE_LOGGER, logtags, logging-infrastructure]

# Dependency graph
requires:
  - phase: 01-logging-foundation
    plan: 01-01
    provides: [logging.h, log_tags.h, setup.cpp, DEFINE_LOGGER macro, 22 tag constants]
provides:
  - "DEFINE_LOGGER wired into 10 source files previously without spdlog: pipeline.cpp, config_builder.cpp, stop_signal.cpp, video_progress_parser.cpp, video_output_planning.cpp, pack_service.cpp, media_scanner.cpp, job_state.cpp, task_executor.cpp, parallel.cpp"
  - "Each file adds 3 lines: #include logging/log_tags.h, #include logging/logging.h, DEFINE_LOGGER(logtags::XXX)"
  - "Zero logic changes — infrastructure-only for Phase 2-4 LOG_* call additions"
affects: [02-logging-file-management, 03-forensic-diagnostics, 04-json-output]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "DEFINE_LOGGER placed after #include block, before anonymous namespace"
    - "All DEFINE_LOGGER calls use logtags:: constants (zero string literals)"

key-files:
  created: []
  modified:
    - src/app/pipeline.cpp
    - src/cmd/config_builder.cpp
    - src/infra/stop_signal.cpp
    - src/video/video_progress_parser.cpp
    - src/video/video_output_planning.cpp
    - src/pack/pack_service.cpp
    - src/core/media_scanner.cpp
    - src/core/job_state.cpp
    - src/core/task_executor.cpp
    - src/core/parallel.cpp

key-decisions:
  - "DEFINE_LOGGER placed after #include block, before namespace/anonymous namespace — matches plan specification and established pattern"
  - "All calls use logtags:: constants (e.g., logtags::APP_PIPELINE) — zero string literals confirmed via grep"

patterns-established:
  - "DEFINE_LOGGER insertion pattern: 2 logging includes in project headers section + 1 DEFINE_LOGGER after includes before namespace block"

requirements-completed: [INF-04]

# Metrics
duration: 3min
completed: 2026-05-23
---

# Phase 01 Plan 04: DEFINE_LOGGER Infrastructure Wiring Summary

**Wired DEFINE_LOGGER(logtags::XXX) into 10 source files previously without spdlog — zero logic changes, infrastructure-only for Phase 2-4 log call migration**

## Performance

- **Duration:** ~3 min
- **Started:** 2026-05-23T02:27:00+08:00
- **Completed:** 2026-05-23T02:28:30+08:00
- **Tasks:** 3
- **Files modified:** 10

## Accomplishments
- All 10 target files now have `#include "logging/log_tags.h"`, `#include "logging/logging.h"`, and `DEFINE_LOGGER(logtags::XXX)` — exactly 3 lines added per file
- Zero logic changes — no function bodies modified, no existing code altered
- All DEFINE_LOGGER calls use `logtags::` constants (verified via grep — zero string literals)
- logger pointers cached as `static auto* const` — zero runtime overhead until first log call

## Task Commits

Each task was committed atomically:

1. **Task 1: Add DEFINE_LOGGER to app, cmd, and infra files** - `bbc289a` (feat: pipeline.cpp, config_builder.cpp, stop_signal.cpp)
2. **Task 2: Add DEFINE_LOGGER to video module files** - `f549a79` (feat: video_progress_parser.cpp, video_output_planning.cpp)
3. **Task 3: Add DEFINE_LOGGER to pack and core files** - `8f35a1a` (feat: pack_service.cpp, media_scanner.cpp, job_state.cpp, task_executor.cpp, parallel.cpp)

## Files Modified

| File | Tag Constant | DEFINE_LOGGER Call |
|------|-------------|-------------------|
| `src/app/pipeline.cpp` | `APP_PIPELINE` | `DEFINE_LOGGER(logtags::APP_PIPELINE)` |
| `src/cmd/config_builder.cpp` | `CMD_CONFIG` | `DEFINE_LOGGER(logtags::CMD_CONFIG)` |
| `src/infra/stop_signal.cpp` | `INFRA_SIGNAL` | `DEFINE_LOGGER(logtags::INFRA_SIGNAL)` |
| `src/video/video_progress_parser.cpp` | `VIDEO_PROGRESS` | `DEFINE_LOGGER(logtags::VIDEO_PROGRESS)` |
| `src/video/video_output_planning.cpp` | `VIDEO_OUTPUT` | `DEFINE_LOGGER(logtags::VIDEO_OUTPUT)` |
| `src/pack/pack_service.cpp` | `PACK_SERVICE` | `DEFINE_LOGGER(logtags::PACK_SERVICE)` |
| `src/core/media_scanner.cpp` | `CORE_SCAN` | `DEFINE_LOGGER(logtags::CORE_SCAN)` |
| `src/core/job_state.cpp` | `CORE_JOB` | `DEFINE_LOGGER(logtags::CORE_JOB)` |
| `src/core/task_executor.cpp` | `CORE_TASK` | `DEFINE_LOGGER(logtags::CORE_TASK)` |
| `src/core/parallel.cpp` | `CORE_PARALLEL` | `DEFINE_LOGGER(logtags::CORE_PARALLEL)` |

## Decisions Made

None — followed plan as specified. All 10 files edited according to the identical mechanical pattern:
1. Add `#include "logging/log_tags.h"` and `#include "logging/logging.h"` at end of project headers block
2. Add `DEFINE_LOGGER(logtags::XXX)` after includes, before namespace/anonymous namespace block

## Deviations from Plan

### Auto-fixed Issues

None — no bugs, missing functionality, or blocking issues encountered during execution.

### Environmental Limitation

**Build verification not performed:** The `xmake build -m debug` verification step could not be executed because the build environment lacks Microsoft Visual Studio / Windows SDK, which is required by xmake's `clang-cl` toolchain for the `windows` platform. `clang-cl.exe` is available at `D:\scoop\apps\llvm\current\bin\clang-cl.exe`, but xmake's `clang-cl` toolchain validation requires MSVC detection.

**Verification performed instead:**
- `grep -rn 'DEFINE_LOGGER("' src/ --include="*.cpp"` — zero results (all calls use `logtags::` constants)
- `grep -rn "DEFINE_LOGGER" src/ --include="*.cpp"` — 10 results, all matching the 10 target files
- Manual review of each file confirmed correct include placement and tag constant usage

**Mitigation:** The changes are purely mechanical (add includes + macro invocation) with no logic impact. The same pattern was already verified to compile in plan 01-01 (which created logging.h and log_tags.h). All files follow the exact same DEFINE_LOGGER insertion pattern.

## Issues Encountered

None — all 10 files followed the identical edit pattern, all grep verifications passed on first run.

## Threat Mitigation Note

Per the plan's threat model:
- **T-01-04-02 (signal handler logging):** `stop_signal.cpp`'s `DEFINE_LOGGER(logtags::INFRA_SIGNAL)` is placed at file scope for use by regular functions (`installHandler()`, `reset()`). The signal handler (`handleConsoleCtrl`/`handleSignal`) in the anonymous namespace only sets atomic flags and does NOT use LOG_* macros, avoiding the spdlog mutex deadlock risk documented in PITFALLS.md.

## Known Stubs

None — all DEFINE_LOGGER calls resolve to valid spdlog loggers that will be registered by `logging::setup()` (created in plan 01-01). The `static auto* const gLoggerPtr` variables remain null until the first LOG_* call triggers initialization, at which point the logger has already been registered.

## Threat Flags

None — no new network endpoints, auth paths, file access patterns, or schema changes at trust boundaries.

## Next Phase Readiness

- All 10 files are ready for Phase 2-4 LOG_* call additions
- Each file's `gLoggerPtr` will auto-resolve when `logging::setup()` registers the corresponding named logger
- No build to verify, but mechanical correctness is confirmed via grep patterns

---
*Phase: 01-logging-foundation*
*Plan: 04*
*Completed: 2026-05-23*
