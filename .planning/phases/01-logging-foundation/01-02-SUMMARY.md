---
phase: 01-logging-foundation
plan: 02
subsystem: logging
tags: [build-system, refactor, prelude, spdlog-active-level]
type: execute
wave: 2
completed_date: 2026-05-23
duration_m: 12
task_count: 3
file_count: 2

requires:
  - plan: 01-01 (logging infrastructure headers + setup.cpp)

provides:
  - SPDLOG_ACTIVE_LEVEL compile-time defines (4 build modes)
  - prelude.cpp refactored to delegate to logging::setup()
  - crash_runtime.cpp compatibility verified (no changes needed)

affects:
  - xmake.lua (build configuration)
  - src/app/prelude.cpp (logging initialization)
  - All targets that use LOG_TRACE/LOG_DEBUG (now stripped in release)

decisions: []

tech-stack:
  added: []
  patterns:
    - "Designated initializers: LogConfig{.verboseEnabled = ..., .verboseEchoEnabled = ..., .colorsEnabled = ...}"
    - "DEFINE_LOGGER placement: after anonymous namespace, before public namespace"
    - "Delegate pattern: prelude.cpp setupLogging() -> logging::setup(LogConfig)"

key-files:
  created: []
  modified:
    - path: xmake.lua
      change: "Added SPDLOG_ACTIVE_LEVEL defines per build mode (D-14)"
      lines: "+13"
    - path: src/app/prelude.cpp
      change: "Refactored setupLogging() to delegate; 12 spdlog::info -> LOG_INFO; removed sink/async includes"
      lines: "+40/-119"
---

# Phase 01 Plan 02: Build Config + Prelude Refactor Summary

**One-liner:** Activated compile-time log stripping (SPDLOG_ACTIVE_LEVEL) in xmake.lua, refactored prelude.cpp to delegate to logging::setup(), and verified crash_runtime.cpp default_logger compatibility.

## Tasks Executed

| # | Task | Type | Commit | Files | Status |
|---|------|------|--------|-------|--------|
| 1 | Add SPDLOG_ACTIVE_LEVEL to xmake.lua | auto | `713242a` | xmake.lua | Done |
| 2 | Refactor prelude.cpp (setupLogging + logConfigSummary) | auto | `6fc0099` | src/app/prelude.cpp | Done |
| 3 | Verify crash_runtime.cpp compatibility | auto | N/A (no changes) | src/infra/crash_runtime.cpp | Done |

## Task Details

### Task 1: SPDLOG_ACTIVE_LEVEL (xmake.lua)

Added 13 lines to `xmake.lua` between the Windows platform defines and `add_requires`:

| Build Mode | SPDLOG_ACTIVE_LEVEL | Effect |
|------------|---------------------|--------|
| `release` | `SPDLOG_LEVEL_INFO` (2) | LOG_TRACE + LOG_DEBUG stripped at compile time |
| `releasedbg` | `SPDLOG_LEVEL_INFO` (2) | LOG_TRACE + LOG_DEBUG stripped at compile time |
| `debug` | `SPDLOG_LEVEL_TRACE` (0) | All log levels retained |
| `coverage` | `SPDLOG_LEVEL_TRACE` (0) | All log levels retained |

The defines are added at global scope via `add_defines()` with `is_mode()` guards, so they apply to all targets (encro, tests, e2e_tests).

### Task 2: prelude.cpp Refactoring

Three categories of changes:

**Includes restructured:**
- Removed: `<spdlog/async.h>`, `<spdlog/logger.h>`, `<spdlog/sinks/basic_file_sink.h>`, `<spdlog/sinks/stdout_color_sinks.h>`, `<spdlog/sinks/stdout_sinks.h>`, `<cstdlib>`, `<memory>`, `<mutex>`, `<system_error>`, `<vector>`
- Added: `"logging/log_tags.h"`, `"logging/logging.h"`, `"logging/setup.h"`, `<filesystem>`
- Kept: `"app/prelude.h"`, `"infra/terminal.h"`, `<spdlog/spdlog.h>`

**setupLogging() simplified (net -79 lines):**
- Removed: `resolveCommonLogDir()` function (40 lines), `readWindowsEnvPath()` helper (13 lines), and the old sink-creation/logger-construction code
- New implementation: 30 lines that delegate to `logging::setup(LogConfig{...})` while preserving the non-verbose early-return path and the verbose log file hint output

**logConfigSummary() migrated to macros:**
- Added `DEFINE_LOGGER(logtags::APP_PRELUDE)` before `namespace prelude`
- 12 `spdlog::info(...)` calls replaced with `LOG_INFO(...)`
- Zero `spdlog::` calls remain in logConfigSummary

### Task 3: crash_runtime.cpp Compatibility

Verification-only task. Confirmed:

| Check | Result |
|-------|--------|
| `tryWriteToLogger()` uses `spdlog::default_logger_raw()` | 1 occurrence at line 30 |
| `setup.cpp` calls `spdlog::set_default_logger()` | 1 occurrence at line 176 |
| `crash_runtime.cpp` has no `DEFINE_LOGGER` | Confirmed (by design) |
| `crash_runtime.cpp` includes `<spdlog/spdlog.h>` | 1 occurrence |

The crash handler continues to access the default logger via `spdlog::default_logger_raw()`, which is set by `logging::setup()` to a valid async_logger sharing the same file sink as all named loggers.

## Deviations from Plan

### Auto-fixed Issues

None — plan executed as written.

### Environment Limitations

- **Build verification skipped:** clang-cl toolchain not available in this agent environment (documented in RESEARCH.md). xmake.lua changes are syntactically verified — `is_mode()` guards and `add_defines()` calls follow standard xmake patterns. Build will be verified in CI/developer environment.

### Plan Miscount

- The plan states "13 个 spdlog::info 调用替换为 LOG_INFO" but the actual `logConfigSummary()` function contains 12 `spdlog::info` calls. All 12 were correctly replaced with `LOG_INFO`.

## Verification Results

| Check | Expected | Actual |
|-------|----------|--------|
| xmake.lua: release mode defines SPDLOG_LEVEL_INFO | Present | Confirmed (line 38) |
| xmake.lua: releasedbg mode defines SPDLOG_LEVEL_INFO | Present | Confirmed (line 40) |
| xmake.lua: debug mode defines SPDLOG_LEVEL_TRACE | Present | Confirmed (line 42) |
| xmake.lua: coverage mode defines SPDLOG_LEVEL_TRACE | Present | Confirmed (line 44) |
| prelude.cpp: no resolveCommonLogDir() | Absent | Confirmed |
| prelude.cpp: setupLogging() delegates to logging::setup() | Present | Confirmed (line 36) |
| prelude.cpp: no spdlog sink/async includes | Absent | Confirmed |
| prelude.cpp: DEFINE_LOGGER present | Present | Confirmed (line 51) |
| prelude.cpp: no spdlog::info() in logConfigSummary | 0 occurrences | Confirmed |
| prelude.cpp: 12 LOG_INFO() in logConfigSummary | 12 occurrences | Confirmed |
| crash_runtime.cpp: default_logger_raw() present | 1 occurrence | Confirmed |
| crash_runtime.cpp: no DEFINE_LOGGER | 0 occurrences | Confirmed |
| setup.cpp: set_default_logger() present | 1 occurrence | Confirmed |

## Known Stubs

None — all LOG_INFO calls have substantive messages, no placeholder text, no empty data sources.

## Threat Flags

None — no new network endpoints, auth paths, file access patterns, or schema changes introduced. All threat surface matches the plan's `<threat_model>` with "accept" disposition.

## Phase 2 Improvements

- **crash handler direct file append (FILE-04):** The crash handler currently uses `spdlog::default_logger_raw()` which works but may be unsafe in signal-handler context due to mutex contention on the shared async_logger. Phase 2 should implement direct file append writing that bypasses spdlog entirely for crash scenarios.

## Self-Check: PASSED

- [x] xmake.lua modified with SPDLOG_ACTIVE_LEVEL defines
- [x] src/app/prelude.cpp refactored successfully
- [x] src/infra/crash_runtime.cpp verified (no changes needed)
- [x] Both commits exist in git log (`713242a`, `6fc0099`)
- [x] No stubs, no threat flags, no unreplaced spdlog:: calls
