---
phase: 02-file-management-observability
plan: 03
subsystem: crash-infra
tags: [crash-handler, file-append, fallback-chain, spdlog-bypass]
requires: [02-01]
provides: [FILE-04]
affects: [src/infra/crash_runtime.cpp, tests/logging_crash_integration_test.cpp]
tech-stack:
  added: []
  patterns: [direct-file-append, 3-tier-fallback, RAII-exception-safety]
key-files:
  created:
    - tests/logging_crash_integration_test.cpp
  modified:
    - src/infra/crash_runtime.cpp
decisions:
  - "Timestamp format: strftime(\"%Y-%m-%dT%H:%M:%S\") via to_time_t + localtime_s/localtime_r (D-15)"
  - "3-tier fallback: direct file -> spdlog -> stderr (D-16)"
  - "All I/O in try/catch(...) — crash handler never terminates from filesystem errors (D-14)"
  - "Direct file append uses std::ofstream(..., std::ios::app) — bypasses spdlog entirely (D-14, Pitfall #10)"
metrics:
  duration: "~15 min"
  completed: "2026-05-23T04:36:00Z"
---

# Phase 2 Plan 3: Crash Handler Direct File Append Summary

**One-liner:** Added a 3-tier fallback chain to the crash handler: direct file append bypassing spdlog, spdlog logger fallback, and stderr ultimate safety net — ensuring crash diagnostics survive async queue drain and spdlog shutdown.

## Tasks Completed

| # | Type | Name | Commit | Key Files |
|---|------|------|--------|-----------|
| 1 | test (RED) | Integration test for crash handler direct file append | `c0f57ab` | `tests/logging_crash_integration_test.cpp` (created) |
| 2 | feat (GREEN) | Implement direct file append fallback in crash handler | `351e22d` | `src/infra/crash_runtime.cpp` (modified) |
| 3 | test | Post-shutdown file append and format verification tests | `38f8926` | `tests/logging_crash_integration_test.cpp` (modified) |

## Verification

- `xmake build encro` — production binary compiles and links without errors
- `xmake run tests "[logging][crash_integration]"` — 32 assertions in 5 test cases, all passed
- `xmake run tests "[crash]"` — 11 assertions in 3 existing crash tests, all passed (no regressions)
- TDD gates: RED commit (`c0f57ab` — test) before GREEN commit (`351e22d` — feat)

## Implementation Details

### tryWriteDirectToLogFile() (src/infra/crash_runtime.cpp)

New function in anonymous namespace implementing direct file append:

1. Calls `logging::currentLogFilePath()` to get the per-run log file path
2. If path unavailable (e.g., before setup or after shutdown), returns false
3. Opens `std::ofstream(path, std::ios::app)` — append mode
4. Formats timestamp manually via `std::chrono::system_clock::to_time_t()` + `localtime_s`/`localtime_r` + `strftime("%Y-%m-%dT%H:%M:%S")`
5. Writes `[{timestamp}] [critical] [infra.crash] {message}\n` — format matches spdlog kLogPattern
6. All operations wrapped in `try/catch(...)` — returns false on any failure

### writeCrashMessage() 3-Tier Chain

```
1) tryWriteDirectToLogFile(message)  — bypasses spdlog, survives shutdown (D-14)
2) tryWriteToLogger(message)         — existing spdlog path (unchanged)
3) writeToStderr(message)            — ultimate fallback (unchanged)
```

Each tier returns false cleanly on failure, allowing the next tier to attempt delivery.

### Integration Tests (tests/logging_crash_integration_test.cpp)

| Test | What it verifies |
|------|-----------------|
| 1 | Direct file append via `std::ofstream` writes to per-run log file; path equivalence between `setup()` and `currentLogFilePath()` |
| 2 | `currentLogFilePath()` returns a valid path that exists on disk after `setup()` |
| 3 | `currentLogFilePath()` returns `std::nullopt` after `shutdown()` — crash handler can detect this |
| 4 | Log file persists on disk after `spdlog::shutdown()` and remains directly appendable (Pitfall #10 prevention) |
| 5 | Crash message format includes `[timestamp]`, `[critical]`, `[infra.crash]` tags and message body (D-15 verification) |

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Test 5 read initial spdlog content before async flush**
- **Found during:** Task 3
- **Issue:** Test 5 checked `initialContent.empty()` immediately after `logging::setup()`, but spdlog's async queue had not flushed the initial `debug("Verbose logging enabled.")` line to disk yet. The file appeared empty.
- **Fix:** Changed test to call `logging::shutdown()` first (which flushes the async queue), then read spdlog's content, then append and verify the crash message.
- **Files modified:** `tests/logging_crash_integration_test.cpp`
- **Commit:** `38f8926`

## Auth Gates

None — no authentication required for this task.

## Known Stubs

None — all implemented functions are complete. `tryWriteDirectToLogFile()` has full implementation with error handling.

## Threat Surface Scan

No new threat surface beyond what the plan's threat model covers (T-02-08 through T-02-SC). The 3-tier fallback design mitigates T-02-09 (DoS via file write failure). Direct file append is only used during crash (terminal process state), so T-02-08 (concurrent write interleaving) is not a concern.

## Self-Check

- [x] `tests/logging_crash_integration_test.cpp` exists on disk
- [x] `src/infra/crash_runtime.cpp` contains `tryWriteDirectToLogFile()` function
- [x] `src/infra/crash_runtime.cpp` contains 3-tier `writeCrashMessage()` chain
- [x] Commit `c0f57ab` exists (RED: test)
- [x] Commit `351e22d` exists (GREEN: feat)
- [x] Commit `38f8926` exists (Task 3: test)
- [x] All 5 integration tests pass (32 assertions)
- [x] All 3 existing crash tests pass (11 assertions, no regressions)
- [x] Production binary (`encro.exe`) compiles and links

## Self-Check: PASSED
