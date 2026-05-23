---
phase: 04-json-tooling
plan: 02
subsystem: logging
type: execute
tags: [json, ndjson, cli, config-chain, retention]

requires: [04-01]
provides: [--log-json CLI flag, dual-sink NDJSON output, NDJSON retention]
affects:
  - src/cmd/cmd.h
  - src/cmd/cmd.cpp
  - src/core/app_context.h
  - src/cmd/config_builder.cpp
  - src/app/prelude.cpp
  - src/logging/setup.h
  - src/logging/setup.cpp
  - tests/logging_file_mgmt_test.cpp

tech-stack:
  added: []
  patterns:
    - "CLI11 boolean flag registration + applyMap lambda"
    - "Designated initializer chain (CmdParseResult -> AppConfig -> LogConfig)"
    - "Per-sink formatter isolation via spdlog::sink::set_formatter()"
    - "NDJSON filename derivation via fs::path::replace_extension()"
    - "Retention pattern extension (OR filter for .log | .ndjson)"

key-files:
  modified:
    - src/cmd/cmd.h
    - src/cmd/cmd.cpp
    - src/core/app_context.h
    - src/cmd/config_builder.cpp
    - src/app/prelude.cpp
    - src/logging/setup.h
    - src/logging/setup.cpp
    - tests/logging_file_mgmt_test.cpp
  created: []

decisions:
  - "D-07: jsonEnabled field on CmdParseResult, AppConfig, and LogConfig structs -- follows existing boolean field patterns exactly"
  - "D-08: --log-json flag is independent of --verbose; gate allows jsonEnabled to proceed without verboseEnabled"
  - "D-03/D-04: Companion .ndjson file with per-sink JsonFormatter; human-readable .log sink now guarded by config.verboseEnabled"
  - "D-13: retainRecentLogs() extended to clean encro_*.ndjson* files alongside encro_*.log* files"

metrics:
  duration: 8.6 minutes
  start: 2026-05-23T11:15:13Z
  completed: 2026-05-23T11:23:47Z
  task_count: 3
  file_count: 8
---

# Phase 4 Plan 02: --log-json CLI flag wiring and setup.cpp integration

Wired `--log-json` boolean CLI flag through the entire config chain (CLI11 -> CmdParseResult -> AppConfig -> LogConfig -> logging::setup()) and integrated JSON file sink creation, gate logic update, and NDJSON retention cleanup into setup.cpp.

## What Was Built

### CLI flag and config chain (Task 1)

- **`CmdParseResult::jsonEnabled`** (`src/cmd/cmd.h`): new boolean field, default false
- **`--log-json` flag** (`src/cmd/cmd.cpp`): registered in GeneralFlags array as a boolean flag (no short flag, no value) with description "enable NDJSON structured log output (one JSON object per line)"
- **`applyMap["--log-json"]`** (`src/cmd/cmd.cpp`): lambda setter `r.jsonEnabled = o->count() > 0`
- **`AppConfig::jsonEnabled`** (`src/core/app_context.h`): new boolean field, default false
- **`config_builder.cpp`**: `config.jsonEnabled = result.jsonEnabled;` propagation
- **`LogConfig::jsonEnabled`** (`src/logging/setup.h`): new boolean field, `{false}` initializer
- **`prelude.cpp` gate**: changed from `!cmd.verbose` to `!cmd.verbose && !cmd.jsonEnabled` -- allows jsonEnabled to proceed without verboseEnabled
- **`prelude.cpp` LogConfig construction**: passes `.jsonEnabled = cmd.jsonEnabled` in designated initializer (correct field ordering: after verboseEchoEnabled, before colorsEnabled)

### JSON file sink and gate integration (Task 2)

- **`setup.cpp` gate** (line 139): changed from `!config.verboseEnabled` to `!config.verboseEnabled && !config.jsonEnabled`
- **Human-readable file sink**: now guarded by `if (config.verboseEnabled)` -- only created when verbose output is requested
- **JSON file sink**: created when `config.jsonEnabled` is true, uses `rotating_file_sink_mt` with same 10MB/3 rotation config as human-readable sink
- **JsonFormatter binding**: `jsonSink->set_formatter(std::make_unique<logging::JsonFormatter>())` on JSON sink only -- per-sink formatter isolation ensures console output remains human-readable
- **NDJSON filename**: derived from `.log` file path via `fs::path::replace_extension(".ndjson")` -- preserves timestamp prefix and PID collision suffix
- **Include**: added `#include "logging/json_formatter.h"` alongside other logging includes

### Retention extension (Task 2)

- **`retainRecentLogs()` filter**: changed from single `.log` check to OR filter accepting both `.log` and `.ndjson` extensions
- Rotated NDJSON files (`encro_*.ndjson.1`, `encro_*.ndjson.2`, `encro_*.ndjson.3`) also included in the "keep 10 most recent" logic

### Test infrastructure update (Task 3)

- **`countEncroFiles()` regex** (`tests/logging_file_mgmt_test.cpp`): extended from `encro_.*\.log.*` to `encro_.*\.(log|ndjson).*` so retention tests correctly count both .log and .ndjson entries

## Sink Matrix

| `--verbose` | `--log-json` | Console sink | Human-readable .log | NDJSON .ndjson |
|-------------|-------------|--------------|---------------------|----------------|
| false       | false       | no           | no                  | no             |
| false       | true        | no           | no                  | yes            |
| true        | false       | no*          | yes                 | no             |
| true + echo | false       | yes          | yes                 | no             |
| true        | true        | no*          | yes                 | yes            |
| true + echo | true        | yes          | yes                 | yes            |

- Console sink only created when `verboseEchoEnabled` is true.

## Full Test Suite

```
All tests passed (3423 assertions in 340 test cases)
```

No regressions. Same assertion count as Plan 04-01 baseline (3423).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed designated initializer ordering in prelude.cpp LogConfig construction**

- **Found during:** Task 1 build
- **Issue:** `jsonEnabled` was placed after `colorsEnabled` in the designated initializer, but the LogConfig struct defines `jsonEnabled` before `colorsEnabled`. clang-cl emitted `-Wreorder-init-list` warning.
- **Fix:** Moved `.jsonEnabled = cmd.jsonEnabled` before `.colorsEnabled = terminal::colorsEnabled()` to match declaration order.
- **Files modified:** `src/app/prelude.cpp`
- **Commit:** `35efa87`

## Self-Check

### Files

```
FOUND: src/cmd/cmd.h
FOUND: src/cmd/cmd.cpp
FOUND: src/core/app_context.h
FOUND: src/cmd/config_builder.cpp
FOUND: src/app/prelude.cpp
FOUND: src/logging/setup.h
FOUND: src/logging/setup.cpp
FOUND: tests/logging_file_mgmt_test.cpp
```

### Commits

```
FOUND: 35efa87 feat(04-02): add --log-json CLI flag and propagate through config chain
FOUND: 3c02746 feat(04-02): integrate JSON file sink, gate logic, and NDJSON retention
FOUND: 663070f test(04-02): update retention test regex to count .ndjson files
```

## Self-Check: PASSED
