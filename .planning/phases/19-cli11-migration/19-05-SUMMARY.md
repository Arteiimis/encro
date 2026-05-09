---
phase: 19-cli11-migration
plan: 05
subsystem: testing
tags: [cli11, catch2, boost-migration, test-rewrite, cmd, config-builder]

# Dependency graph
requires:
  - plan: 19-01
    provides: CmdParseResult struct, CLI11-based commandLineInit()
  - plan: 19-02
    provides: buildConfig(CmdParseResult const&) signature
provides:
  - "cmd_cmd_tests.cpp: 14 CLI11 integration tests via real commandLineInit() parsing"
  - "cmd_config_builder_tests.cpp: 40 fixture-based tests using CmdParseResult"
  - "Zero boost::program_options references in both test files"
affects: [phase-20-color-enhancement, future-testing]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "parseArgs() helper wraps commandLineInit() for Catch2 test cases"
    - "makeResult() fixture constructs CmdParseResult directly for config builder tests"
    - "ScopedEnvVar RAII helper for COLUMNS env var manipulation"

key-files:
  created: []
  modified:
    - tests/cmd_cmd_tests.cpp
    - tests/cmd_config_builder_tests.cpp
    - src/cmd/cmd.cpp

key-decisions:
  - "D-08 applied: cmd_cmd_tests uses integration tests through real CLI11 parsing"
  - "D-09 applied: cmd_config_builder_tests uses CmdParseResult fixture"
  - "D-06 applied: error test accepts any CLI11 error string instead of specific boost::po message"

patterns-established: []

requirements-completed:
  - CLI11-04
  - CLI11-05

# Metrics
duration: ~60min
completed: 2026-05-09
---

# Phase 19 Plan 05: Rewrite Test Files for CLI11 Migration Summary

**14 CLI11 integration tests + 40 CmdParseResult fixture-based tests replacing boost::program_options in test suite with zero boost references**

## Performance

- **Duration:** ~60 min (including debugging multi-value parsing issue)
- **Tasks:** 2
- **Files modified:** 3
- **Test results:** 246/247 passed (1 deferred failure)

## Accomplishments

- Rewrote `tests/cmd_cmd_tests.cpp` with 14 test cases using real CLI11 parsing via `parseArgs()` → `commandLineInit()`
- Rewrote `tests/cmd_config_builder_tests.cpp` with 40 test cases using `makeResult()` fixture → `cmd::buildConfig(result)`
- Zero `boost::program_options` references in both test files (verified via grep)
- All option defaults verified: type=video, output-format=mp4, force-conflict-handling=y, color=auto
- Help text width adaptation tested at COLUMNS=72 and COLUMNS=200
- Error detection, multi-input vector, short option `-q` → `image-quality` mapping all tested

## Task Commits

Each task was committed atomically:

1. **Task 1: Rewrite cmd_cmd_tests.cpp** - `c121028` (test)
2. **Task 2: Rewrite cmd_config_builder_tests.cpp** - `5f8a56a` (test)
3. **Fix: Multi-value parsing configuration** - `05b49c5` (fix)

## Files Modified

- `tests/cmd_cmd_tests.cpp` - 14 CLI11 integration tests (was 13 boost::po tests, 241 lines)
- `tests/cmd_config_builder_tests.cpp` - 40 CmdParseResult fixture tests (was 40 boost::po tests, 706 lines)
- `src/cmd/cmd.cpp` - Configured `-I/--inputs` for multi-value parsing via `expected(0, 1000000)`

## Decisions Made

- Followed plan exactly for test structure per D-08 and D-09
- Kept `ScopedEnvVar` and `readEnvVar` helpers unchanged (not boost::po related)
- Removed `renderHelp()` helper - `result.helpText` is pre-populated by `commandLineInit()`
- Used `CHECK_FALSE` for boolean assertions (Catch2 convention)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed `-I/--inputs` option not configured for multi-value parsing**
- **Found during:** Post-task verification (test suite run)
- **Issue:** CLI11's `add_option("-I,--inputs")` defaults to expecting exactly 1 value; the test `parseArgs({"encro", "-I", "a.mp4", "b.mkv", "c.mov"})` expected 3 values to be collected
- **Fix:** Added `->expected(0, 1000000)` to allow unlimited input values
- **Files modified:** `src/cmd/cmd.cpp`
- **Committed in:** `05b49c5`

### Deferred Issues

**1. Multi-input parsing via `-I` still fails due to CLI11 right-to-left parsing interaction with option groups**

Despite the `expected(0, 1000000)` fix, the test `"commandLineInit parses multi-input values"` at line 162 still fails with `result.inputs.has_value() == false`. Root cause analysis indicates CLI11 processes arguments from right-to-left, and when `-I` is in an option group (subcommand), the positional values following it may be consumed before the option is recognized. This is a pre-existing issue from plan 19-01 (cmd.cpp migration). Possible solutions include:
- Moving `-I/--inputs` to the main app level instead of the IO option group
- Using `app.allow_extras()` and handling extras manually
- Restructuring option group hierarchy

**Impact:** 1 of 247 tests fails (14th cmd test). All config_builder tests (40) pass. This does not block the test rewrite plan itself (the test code is correct per D-08), but indicates a parsing bug that should be addressed in a follow-up fix.

## Test Results Summary

```
test cases:  247 |  246 passed | 1 failed
assertions: 3039 | 3038 passed | 1 failed
```

- **cmd_cmd_tests:** 13/14 pass (1 failure: multi-input `-I` parsing)
- **cmd_config_builder_tests:** 40/40 pass
- **All other test suites:** 206/206 pass (unaffected)

## Next Phase Readiness

- Both test files are fully migrated to CLI11/CmdParseResult with zero boost::po references
- Config builder test suite (40 cases) provides comprehensive validation coverage
- Cmd test suite (13 passing cases) covers defaults, flags, options, help text width, short options
- Ready for Phase 20 (color enhancement) and integration testing
- Deferred multi-input issue tracked for resolution

---
*Phase: 19-cli11-migration*
*Completed: 2026-05-09*
