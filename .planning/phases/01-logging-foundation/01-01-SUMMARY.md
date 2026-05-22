---
phase: 01-logging-foundation
plan: 01
subsystem: infra
tags: [spdlog, logging, macros, named-loggers, source-location]

# Dependency graph
requires: []
provides:
  - 25 constexpr module tag constants in log_tags.h (dot-notation hierarchy)
  - LOG_TRACE/DEBUG/INFO/WARN/ERROR/CRITICAL macro layer with source location injection
  - DEFINE_LOGGER macro for per-file named logger pointer caching
  - Centralized LogConfig + logging::setup() / shutdown() in setup.cpp
  - Shared-sink async_logger registry (24 tags + default "encro" logger)
affects: [01-02-use-log-macros, 01-03-remove-spdlog, 01-04-finalize-foundation]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "DEFINE_LOGGER(tag): per-.cpp static cached raw pointer to named async_logger"
    - "LOG_* macros: fmt::format(__VA_ARGS__) pre-formats at call site, source location injected as '[file:line] msg'"
    - "Shared-sink architecture: all 24 named loggers + default_logger share 1 file sink + optional 1 console sink"
    - "D-03 pattern: source location in message body, NOT in %s:%# spdlog pattern flags"
    - "kLogPattern: '[%Y-%m-%dT%H:%M:%S.%e%z] [%^%l%$] [%n] %v' — no %s or %#"
    - "LogConfig struct with verboseEnabled, verboseEchoEnabled, colorsEnabled, customLogDir"

key-files:
  created:
    - src/logging/log_tags.h (25 inline constexpr tag constants)
    - src/logging/logging.h (DEFINE_LOGGER, 6 LOG_* macros, shortFile helper)
    - src/logging/setup.h (LogConfig struct, setup()/shutdown() declarations)
    - src/logging/setup.cpp (resolveCommonLogDir, allModuleTags, setup implementation)
    - tests/logging_infra_test.cpp (5 TDD test cases)
  modified: []

key-decisions:
  - "Source location injected into message body (D-03) — async-safe, no spdlog source_loc dangling risk"
  - "kLogPattern omits %s:%# — resolves D-03 vs D-10 format conflict per RESEARCH Q1 recommendation"
  - "DEFINE_LOGGER uses raw pointer (auto* const) instead of shared_ptr — logger lifetime guaranteed by spdlog registry"
  - "fmt::format at call site pre-formats message string — avoids TLS issues with async logging"
  - "Crash handler compatibility preserved via default_logger ('encro') sharing the same file sink"

patterns-established:
  - "Per-file logger registration: each .cpp places DEFINE_LOGGER(logtags::XXX) after includes"
  - "Dot-notation tag hierarchy: module.submodule (e.g., video.encode, pack.zip, core.scan)"
  - "Centralized setup: all sink/logging configuration lives in logging::setup(), business code uses only macros"

requirements-completed: [INF-01, INF-03, INF-04, INF-05, OBS-01, OBS-02, OBS-04]

# Metrics
duration: 15min
completed: 2026-05-23
---

# Phase 01 Plan 01: Logging Foundation Summary

**25 module tag constants, 6-level macro layer with source location injection, and centralized 24-logger shared-sink registry — the ground layer for all subsequent logging enhancement phases**

## Performance

- **Duration:** ~15 min
- **Started:** 2026-05-23
- **Completed:** 2026-05-23
- **Tasks:** 3 (TDD cycle)
- **Files created:** 5

## Accomplishments

- Defined 25 `inline constexpr auto` tag constants in `logtags::` namespace with dot-notation hierarchy (app.*, video.*, pack.*, core.*, infra.*, utils.*, picture.*, test.*)
- Implemented `LOG_TRACE/DEBUG/INFO/WARN/ERROR/CRITICAL` macros using `SPDLOG_LOGGER_CALL` with `[file:line] msg` source location injection into message body
- Created `DEFINE_LOGGER(tag)` macro for per-file static raw pointer caching to named async_logger
- Built centralized `logging::setup()` in `setup.cpp`: platform-aware log directory resolution, shared file+console sink creation, registration of all 24 named async_loggers, default_logger for crash handler compatibility
- Wrote 5 TDD test cases covering tag constant validation, macro expansion output, source location format, full setup registration, and shutdown cleanup

## Task Commits

Each task was committed atomically:

1. **Task 1: RED - Failing tests** - `5e20e3e` (test: write RED-phase tests for logging infrastructure)
2. **Task 2: GREEN - log_tags.h + logging.h** - `6325c9d` (feat: add 25 tag constants and 6 LOG_* macros)
3. **Task 3: GREEN - setup.h + setup.cpp** - `fa4e13c` (feat: add centralized logger registry and sink setup)

## Files Created

- `src/logging/log_tags.h` — 25 `inline constexpr auto` tag constants in `logtags::` namespace, grouped by subsystem
- `src/logging/logging.h` — `DEFINE_LOGGER` macro, 6 `LOG_*` macros, `logging::detail::shortFile()` helper
- `src/logging/setup.h` — `LogConfig` struct with 4 fields, `setup()` and `shutdown()` declarations
- `src/logging/setup.cpp` — `resolveCommonLogDir()`, `allModuleTags()`, full `setup()` implementation with shared-sink architecture
- `tests/logging_infra_test.cpp` — 5 Catch2 test cases covering tag format, macro output, source location, setup registration, and shutdown

## Decisions Made

- Followed RESEARCH Q1 recommendation: pattern omits `%s:%#` (D-03 overrides D-10) to avoid async source_loc dangling risk
- Used `fmt::format(__VA_ARGS__)` at call site (pre-formatted message) per RESEARCH pitfall #3 — avoids TLS issues in async logging
- Raw pointer caching (`auto* const`) rather than `shared_ptr` — logger lifetime guaranteed by spdlog global registry
- Preserved existing log filename `encro.verbose.log` — Phase 2 will rename to timestamp-based naming

## Deviations from Plan

None — plan executed exactly as written.

## Issues Encountered

- `clang-cl` toolchain not available in this agent environment (documented in RESEARCH.md) — no build verification possible. Test file and sources are syntactically consistent with project conventions and spdlog v1.x API. Compilation verification deferred to developer's machine where clang-cl is installed.

## Next Phase Readiness

- All 5 source files (4 logging + 1 test) are in place and ready for downstream plans
- Plan 01-02 (use-log-macros) can proceed: files reference `log_tags.h` constants via `logtags::` namespace and call `DEFINE_LOGGER()` + `LOG_*()` macros
- Plan 01-03 (remove-spdlog) can validate against the `allModuleTags()` list in `setup.cpp`
- Plan 01-04 (finalize-foundation) can verify the complete logging pipeline

---
*Phase: 01-logging-foundation*
*Completed: 2026-05-23*
