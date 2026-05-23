---
phase: 04-json-tooling
plan: 01
subsystem: logging
type: tdd
tags: [json, ndjson, formatter, spdlog, boost-json]

requires: []
provides: [logging::JsonFormatter]
affects: []

tech-stack:
  added: []
  patterns:
    - "spdlog::formatter subclass with per-sink set_formatter()"
    - "boost::json::object + serialize() for NDJSON output"
    - "optional field extraction via manual string_view parsing"

key-files:
  created:
    - src/logging/json_formatter.h
    - tests/logging_json_test.cpp
  modified: []

decisions:
  - "D-01: boost::json::object + boost::json::serialize() for JSON construction -- no hand-rolled escaping"
  - "D-02: Fixed fields read from spdlog::details::log_msg struct members -- never regex on formatted text"
  - "D-05: elapsed_ms extracted from 'completed in Xms' pattern via manual string parsing"
  - "D-11: error_context extracted via rfind(' [context:') + split by ' > ' + strip suffix from message"

metrics:
  duration: ~8 minutes
  start: 2026-05-23T11:02:10Z
  completed: 2026-05-23T11:09:55Z
  task_count: 3
  file_count: 2
  test_count: 15
  assertion_count: 64
  rgx_test_total: 3423
---

# Phase 4 Plan 01: JsonFormatter Summary

Custom `spdlog::formatter` subclass producing one-line NDJSON from `log_msg` struct fields using `boost::json::serialize()`.

## What Was Built

**JsonFormatter** (`src/logging/json_formatter.h`) -- a header-only `spdlog::formatter` final subclass that:

- Reads fixed fields (`timestamp`, `level`, `module`, `source`, `message`) directly from `spdlog::details::log_msg` struct members (D-02)
- Extracts optional `elapsed_ms` (int64) from `"completed in Xms"` pattern in `msg.payload`
- Extracts optional `error_context` (string array) from `" [context: ...]"` suffix using `rfind()` + split by `" > "` (D-11)
- Strips the context suffix from the `message` field
- Uses `boost::json::object` + `boost::json::serialize()` for all JSON escaping (backslashes, CJK Unicode, embedded quotes, newlines per D-09)
- Appends `'\n'` for NDJSON line delimiter
- Implements `clone()` returning an independent instance

## TDD Gate Compliance

```
4ec53ac test(04-01): add failing JsonFormatter test suite (RED gate)       -- 15 failing tests
fe17ca0 feat(04-01): implement JsonFormatter (GREEN gate)                   -- 64 assertions green
7356c17 refactor(04-01): extract levelSv local                              -- full suite 3423/3423
```

All three gates present in order. RED gate confirmed failing (missing `json_formatter.h`). GREEN gate confirmed passing (15 tests, 64 assertions). REFACTOR gate confirmed passing (full suite, no regressions).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed level name mismatch: `"warn"` vs `"warning"`**
- **Found during:** Task 2 GREEN run
- **Issue:** `spdlog::level::to_string_view(spdlog::level::warn)` returns `"warning"` (not `"warn"`) in spdlog v1.15.1
- **Fix:** Updated Test 2 expected value from `"warn"` to `"warning"`
- **Files modified:** `tests/logging_json_test.cpp`
- **Commit:** `fe17ca0`

**2. [Rule 1 - Bug] Fixed logger name collision in level-strings test**
- **Found during:** Task 2 GREEN run (first build)
- **Issue:** Test 2 created logger with hardcoded name `"level_tester"` in each iteration, causing "logger already exists" on second iteration
- **Fix:** Changed logger name to use unique `lvlName` variable (`"level_tester_" + expected`) before constructor call
- **Files modified:** `tests/logging_json_test.cpp`
- **Commit:** `fe17ca0`

## Self-Check

### Files

```
FOUND: src/logging/json_formatter.h
FOUND: tests/logging_json_test.cpp
FOUND: .planning/phases/04-json-tooling/04-01-SUMMARY.md
```

### Commits

```
FOUND: 4ec53ac test(04-01): add failing JsonFormatter test suite (RED gate)
FOUND: fe17ca0 feat(04-01): implement JsonFormatter (GREEN gate for NDJSON logging)
FOUND: 7356c17 refactor(04-01): extract levelSv local to avoid double to_string_view call
```

## Self-Check: PASSED
