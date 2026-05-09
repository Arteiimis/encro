---
phase: 20-cli-color-deepening
plan: 01
subsystem: infra
tags: [fmt, terminal, MessageKind, enum, TDD, ANSI]

# Dependency graph
requires: []
provides:
  - "MessageKind enum extended with 5 new values (Usage, OptionGroup, OptionName, OptionDesc, Version) for semantic help/version coloring"
  - "styleFor() mappings for all 5 new MessageKind values (steel_blue+bold, light_cyan, empty)"
  - "defaultBadgeLabel() returning empty for all 5 new kinds (no badge prefixes on help/version)"
  - "13 terminal test cases verifying all new enum→style→badge mappings"
affects:
  - "20-02 (formatter_fn color injection depends on these enum values and styleFor() mappings)"
  - "20-03 (--version output depends on MessageKind::Version)"

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Additive-only enum extension: 5 values appended at MessageKind end, indices 0-7 preserved"
    - "styleFor() case-per-value switch pattern, defaultBadgeLabel() empty-return for non-badge kinds"

key-files:
  created: []
  modified:
    - "src/infra/terminal.h - MessageKind enum extended from 8 to 13 values"
    - "src/infra/terminal.cpp - styleFor() +5 cases, defaultBadgeLabel() +5 cases"
    - "tests/infra/terminal_tests.cpp - 9 new test cases for MessageKind extension"
    - "tests/cmd_cmd_tests.cpp - fix pre-existing 2-arg→3-arg commandLineInit call site"

key-decisions:
  - "TDD RED→GREEN cycle for enum+style extension — tests fail before implementation, pass after"
  - "No REFACTOR needed — implementation is minimal and follows existing patterns exactly"
  - "defaultBadgeLabel tests use indirect `terminal::format()` approach since function is in anonymous namespace"

patterns-established:
  - "TDD for C++ enum extensions: RED adds enum values+tests (compile-but-fail-default), GREEN adds switch cases"
  - "Indirect testing of anonymous-namespace functions via `terminal::format()` (which calls defaultBadgeLabel internally)"

requirements-completed:
  - COLR-02

# Metrics
duration: 8 min
completed: 2026-05-10
---

# Phase 20 Plan 01: MessageKind Extension Summary

**MessageKind enum extended from 8 to 13 values with steel_blue+bold, light_cyan, and empty style mappings — TDD cycle with 9 new test cases**

## Performance

- **Duration:** 8 min
- **Started:** 2026-05-10T00:23:54Z
- **Completed:** 2026-05-10T00:32:23Z
- **Tasks:** 1 TDD feature (RED → GREEN)
- **Files modified:** 4

## Accomplishments
- Extended MessageKind enum with 5 new values: Usage, OptionGroup, OptionName, OptionDesc, Version (appended at end, indices 0–7 preserved)
- Added styleFor() mappings: Usage/OptionGroup/Version → steel_blue+bold, OptionName → light_cyan, OptionDesc → empty
- Added defaultBadgeLabel() mappings: all 5 new kinds return empty (no badge prefix on help/version output)
- Added 9 new test cases covering all 5 enum values, badge prefix behavior, and colorsEnabled() gating

## Task Commits

1. **RED: Add failing tests** — `48b8731` (test)
   - Added 5 enum values to MessageKind (compile-but-fail-default)
   - Added 9 test cases — 4 fail (no ANSI from empty style), 5 pass (correct default behavior)
   - [Rule 3] Fixed pre-existing cmd_cmd_tests.cpp 2-arg→3-arg commandLineInit call

2. **GREEN: Implement mappings** — `ab1e3e4` (feat)
   - Added 5 cases to styleFor() switch
   - Added 5 cases to defaultBadgeLabel() switch
   - All 24 assertions pass (6 original + 9 new = 15 test cases)

**Plan metadata:** to be committed after SUMMARY.md

## Files Created/Modified
- `src/infra/terminal.h` — MessageKind enum: 8→13 values (Usage, OptionGroup, OptionName, OptionDesc, Version appended after Heading)
- `src/infra/terminal.cpp` — styleFor(): +5 cases (steel_blue+bold, light_cyan, empty), defaultBadgeLabel(): +5 cases (all empty)
- `tests/infra/terminal_tests.cpp` — +9 test cases: 5 styleFor(), 2 badge prefix, 2 styledText() gating
- `tests/cmd_cmd_tests.cpp` — Fix pre-existing call site (add introLine="" to commandLineInit)

## Decisions Made
- No REFACTOR phase needed — implementation is minimal (10 new switch case lines in each function) and follows existing patterns exactly
- defaultBadgeLabel() tested indirectly via `terminal::format()` since the function is in an anonymous namespace and not directly callable from tests
- All 5 new MessageKind values use empty badge labels — confirmed via format() output containing no `[usage]`/`[optiongroup]` prefix

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Fixed pre-existing cmd_cmd_tests.cpp compilation error**
- **Found during:** RED phase setup (before any plan changes)
- **Issue:** `commandLineInit()` called with 2 args but Phase 19 changed API to require 3 (`int argc, char* argv[], std::string const& introLine`)
- **Fix:** Added `""` as the 3rd argument at line 82
- **Files modified:** `tests/cmd_cmd_tests.cpp`
- **Verification:** `xmake build tests` succeeds, 3033+ assertions pass
- **Committed in:** `48b8731` (part of RED phase commit)

---

**Total deviations:** 1 auto-fixed (Rule 3 — blocking)
**Impact on plan:** Trivial test call-site fix necessary to enable test build. No scope creep.

## Issues Encountered
- Pre-commit clang-format hook aborted first commit attempt (conflict on terminal_tests.cpp). Resolved by re-applying changes and re-committing — second attempt succeeded.
- Pre-existing `cmd_cmd_tests.cpp` compilation error blocked test execution — fixed as Rule 3 deviation.

## Next Phase Readiness
- MessageKind enum with 13 values is available for Plans 20-02 (formatter_fn color injection) and 20-03 (--version output)
- All 5 new kinds have verified styleFor() mappings and empty badge labels
- Zero consumer impact — additive extension only, all existing code paths unchanged

---
*Phase: 20-cli-color-deepening*
*Completed: 2026-05-10*

## Self-Check: PASSED

- `20-01-SUMMARY.md` exists on disk
- `48b8731` (RED/test) — confirmed in git log
- `ab1e3e4` (GREEN/feat) — confirmed in git log
- `8972ce7` (docs/metadata) — confirmed in git log
- TDD gate sequence: test(20-01) → feat(20-01) ✓
- MessageKind enum: 13 values (8 original + 5 new) ✓
- 24/24 terminal assertions pass ✓
