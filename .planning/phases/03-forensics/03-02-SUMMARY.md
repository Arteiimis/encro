---
phase: 03-forensics
plan: 02
subsystem: logging
tags:
  - forensics
  - error-context
  - macro-injection
  - environment-snapshot
  - lock-free
provides:
  - LOG_ERROR/LOG_CRITICAL context chain injection
  - Environment snapshot (captureEnvironmentSnapshot)
  - EncodingState subprocess fields
  - ExecResult pid tracking
requires:
  - 03-01 (ScopedErrorContext + TLS context stack)
affects:
  - src/logging/logging.h
  - src/logging/setup.h
  - src/logging/setup.cpp
  - src/core/app_context.h
  - src/utils/utils.h
  - src/utils/utils.cpp
tech-stack:
  added: []
  patterns:
    - "do-while(0) macro wrapping for multi-statement LOG_ERROR/LOG_CRITICAL"
    - "Lock-free atomic pointer reads (memory_order_acquire/release)"
    - "Context chain resolved at macro call site before async queue"
    - "Environment snapshot emitted as separate INFO line after error"
key-files:
  created:
    - tests/logging_snapshot_test.cpp
  modified:
    - src/logging/logging.h
    - src/logging/setup.h
    - src/logging/setup.cpp
    - src/core/app_context.h
    - src/utils/utils.h
    - src/utils/utils.cpp
    - tests/logging_error_context_test.cpp
    - src/picture/picture_compress.cpp
    - src/video/video_info.cpp
    - src/video/video_encode_runner.cpp
key-decisions:
  - "LOG_ERROR/LOG_CRITICAL use do-while(0) blocks with __encro_ prefixed temporaries"
  - "Environment snapshot stored in module-level EnvironmentSnapshot struct for testability"
  - "ExecResult.pid typed as std::optional<int> (not sentinel value)"
  - "captureEnvironmentSnapshot uses lock-free atomics -- safe on error paths holding mutexes"
  - "Forensic context via void* pointers to avoid circular dependency between logging and video modules"
metrics:
  duration: ~15m
  completed-at: "2026-05-23"
  tasks: 3
  files_changed: 11
---

# Phase 3 Plan 2: LOG_ERROR/LOG_CRITICAL Context Chain Injection Summary

Implemented LOG_ERROR/LOG_CRITICAL macro modifications that automatically append TLS context chains and trigger environment snapshots, plus infrastructure extensions (EncodingState subprocess fields, ExecResult.pid) for forensic diagnostics.

## One-Liner

LOG_ERROR/LOG_CRITICAL macros inject context chains and environment snapshots via do-while(0) blocks with lock-free atomic pointer reads.

## What Was Built

### LOG_ERROR/LOG_CRITICAL Macro Modifications (`src/logging/logging.h`)

- Replaced single `SPDLOG_LOGGER_CALL` with `do { ... } while(0)` blocks
- Context chain appended via `formatContextChain()` at macro expansion site (calling thread)
- Environment snapshot triggered via `captureEnvironmentSnapshot()` and emitted as separate INFO line
- `__encro_ctx_chain` and `__encro_snapshot` temporary variables for collision-free names
- LOG_INFO, LOG_WARN, LOG_DEBUG, LOG_TRACE unchanged -- context only on LOG_ERROR/LOG_CRITICAL

### Environment Snapshot (`src/logging/setup.h`, `src/logging/setup.cpp`)

- `EnvironmentSnapshot` struct with active-slots, pending, finished, subprocess info
- `setForensicAppContext(void*)` -- stores app context for pipeline type access
- `setForensicExecContext(void*)` -- stores exec context (called in Plan 03-03)
- `setForensicSnapshotData()` / `clearForensicSnapshotData()` -- test-only direct state setters
- `captureEnvironmentSnapshot()` -- lock-free snapshot builder:
  - Returns "" when no AppContext set
  - Returns minimal "Environment: pipeline=X (no encoding slots)" when no encoding active
  - Returns detailed "Environment: active-slots=N/M pending=P finished=F subprocess=[pid=X cmd='...']" when encoding

### Infrastructure Extensions

- **EncodingState** (`src/core/app_context.h`): Added `std::optional<int> subprocessPid` and `std::optional<std::string> subprocessCmdline`
- **ExecResult** (`src/utils/utils.h`): Added `std::optional<int> pid`
- **exec2Impl** (`src/utils/utils.cpp`): Captures `process.id()` on both Windows and POSIX, included in all return paths (normal exit, stop-request termination, timeout detach)
- **Structured bindings**: Fixed 4 call sites that destructured 2-field ExecResult into 3 fields

### Tests

- **error_context test** (Tests 12-16): LOG_ERROR/CRITICAL context chain injection, nested ordering, LOG_INFO exclusion
- **snapshot test** (Tests 1-5): Empty state, minimal snapshot, detailed snapshot with all fields, null encoding safety, end-to-end LOG_ERROR with snapshot ordering

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed structured bindings for new ExecResult field**

- **Found during:** Task 2 (GREEN phase)
- **Issue:** `ExecResult` gained a third field (`pid`), but `picture_compress.cpp`, `video_info.cpp`, and `video_encode_runner.cpp` used 2-element structured bindings `auto const [exitCode, output]` and `auto const [exitCode, _]`.
- **Fix:** Updated all 4 call sites to 3-element bindings: `auto const [exitCode, output, pid]` and `auto const [exitCode, _, pid]`.
- **Files modified:** src/picture/picture_compress.cpp, src/video/video_info.cpp, src/video/video_encode_runner.cpp

## TDD Gate Compliance

- **RED gate:** commit `a02494f` -- snapshot tests fail to compile, error_context tests compile but assertions fail
- **GREEN gate:** commit `fc9548b` -- all 21 tests pass (16 error_context + 5 snapshot)
- **REFACTOR gate:** commit `9820953` -- clang-format applied, all tests still pass

## Test Results

```
All tests passed (41 assertions in 16 test cases) [logging][error_context]
All tests passed (18 assertions in 5 test cases) [logging][snapshot]
Full suite: All tests passed (3359 assertions in 325 test cases)
clang-format check passed for 122 files
```

## Commits

| Hash | Type | Message |
|------|------|---------|
| a02494f | test | add failing tests for LOG_ERROR context chain and environment snapshot |
| fc9548b | feat | implement LOG_ERROR/LOG_CRITICAL context chain injection and environment snapshot |
| 9820953 | refactor | apply clang-format, verify macros use do-while(0) and __encro_ prefix |

## Self-Check

- [x] tests/logging_snapshot_test.cpp exists (created)
- [x] tests/logging_error_context_test.cpp contains Tests 12-16
- [x] src/logging/logging.h has modified LOG_ERROR/LOG_CRITICAL macros
- [x] src/logging/setup.h has EnvironmentSnapshot + forensic API declarations
- [x] src/logging/setup.cpp has captureEnvironmentSnapshot implementation
- [x] src/core/app_context.h has subprocessPid + subprocessCmdline fields
- [x] src/utils/utils.h has ExecResult.pid field
- [x] src/utils/utils.cpp captures PID in exec2Impl
- [x] Commit a02494f exists (RED)
- [x] Commit fc9548b exists (GREEN)
- [x] Commit 9820953 exists (REFACTOR)
- [x] All 21 tests pass (16 error_context + 5 snapshot)
- [x] Full test suite green (325 test cases)
- [x] clang-format check passes
- [x] East const, trailing return, PascalCase, camelCase conventions followed
- [x] do-while(0) pattern with __encro_ prefix on temporaries
- [x] captureEnvironmentSnapshot uses lock-free atomic reads only
