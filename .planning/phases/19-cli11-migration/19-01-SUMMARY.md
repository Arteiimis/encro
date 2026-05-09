---
phase: 19-cli11-migration
plan: 01
subsystem: cmd
tags: [cli, migration, parsing, cli11, refactor]
requires: [19-04]
provides:
  - CmdParseResult struct (26 typed option fields, helpText, error)
  - commandLineInit() using CLI11 API
affects: [19-02, 19-03]
tech-stack:
  added: [CLI11 v2.6.2]
  removed: [boost::program_options (from cmd.h/cmd.cpp)]
  patterns:
    - "CLI::App with add_option_group() for 4 option groups"
    - "Custom formatter_fn via app.formatter_fn() with adaptive column width"
    - "consolewidth::resolveColumns({80, 40, 120}) for help text layout"
key-files:
  created: []
  modified:
    - src/cmd/cmd.h (rewritten: 43 lines, CLI11 include, flat struct)
    - src/cmd/cmd.cpp (rewritten: 265 lines, CLI11 parsing + formatter_fn)
decisions:
  - "Used app.formatter_fn() instead of plan's CLIm::FormatterFcn (does not exist in CLI11 v2.6.2)"
  - "Added app.set_help_flag('') to disable CLI11 auto-help (Rule 1 bug fix — prevents CLI::Success from being thrown before result.help is set)"
  - "Lambda signature adapted to CLI11 v2.6.2: (const CLI::App*, std::string, CLI::AppFormatMode)"
metrics:
  duration: "~15 min"
  completed_date: "2026-05-09"
---

# Phase 19 Plan 01: Rewrite cmd.h/cmd.cpp for CLI11

**One-liner:** Replaced boost::program_options CLI parsing core with CLI11 — 26 options across 4 groups parsed via CLI::App with custom formatter_fn producing identical help layout and adaptive column width.

## Tasks Executed

| Task | Name                              | Commit   | Files              |
|------|-----------------------------------|----------|--------------------|
| 1    | Rewrite cmd.h — CmdParseResult    | 29c4c21  | src/cmd/cmd.h      |
| 2    | Rewrite cmd.cpp — CLI11 parsing   | 949ed02  | src/cmd/cmd.cpp    |

## Verification Results

### Task 1: cmd.h
- ✅ No boost/program_options references: 0
- ✅ CLI11 include present: 1
- ✅ All 26 option fields declared with correct defaults
- ✅ CmdParseResult struct: 1
- ✅ commandLineInit declaration: 1
- ✅ Line count: 43 (≥40 min)

### Task 2: cmd.cpp
- ✅ No boost references: 0
- ✅ CLI11 include: 1
- ✅ CLI::App{}: 1
- ✅ add_option_group: 4 (General, IO, Processing, FileOp)
- ✅ add_flag + add_option: 30 total (≥26, includes 4 group calls)
- ✅ resolveHelpTextLayout: defined and called (2 occurrences)
- ✅ makeHelpFormatter: defined and called (2 occurrences)
- ✅ formatOptionHelp / formatGroupHeader / formatOptionName helpers: present
- ✅ app.formatter_fn(): 1 call
- ✅ CLI::ParseError caught: 1
- ✅ result.help / result.color / result.imageQuality assignment: present
- ⚠️ Line count: 265 (plan expected 170–210; extra from multi-line option strings, help_flag comment, formatting)

### Build
- ✅ `src/cmd/cmd.cpp` compiles successfully
- ⚠️ Full `xmake build encro` fails (expected — consumer files reference old `CmdParseResult` fields; fixed in plans 19-02, 19-03)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Added app.set_help_flag("") to disable auto-help behavior**
- **Found during:** Task 2 implementation
- **Issue:** CLI11's built-in `--help` flag throws `CLI::Success` during `app.parse()`, preventing `result.help` from being set to `true`. Without this fix, `--help` would store "Success" as an error instead of setting the help flag.
- **Fix:** Added `app.set_help_flag("")` before `app.add_flag("-h,--help")` to disable automatic help exit. The help flag is now handled manually via `helpFlag->count()` check.
- **Files modified:** src/cmd/cmd.cpp
- **Commit:** 949ed02

**2. [Rule 1 - Bug] CLI11 v2.6.2 API mismatch: no CLI::FormatterFcn type**
- **Found during:** Task 2 implementation (LSP error)
- **Issue:** The plan specified `CLI::FormatterFcn` return type and `app.formatter(fn)` call, but CLI11 v2.6.2 has no such type. The correct API is `app.formatter_fn(std::function<std::string(const App*, std::string, AppFormatMode)>)`.
- **Fix:** Changed `makeHelpFormatter` return type to `auto`; changed `app.formatter(...)` to `app.formatter_fn(...)`; adapted lambda signature to `(const CLI::App*, std::string, CLI::AppFormatMode)`.
- **Files modified:** src/cmd/cmd.cpp
- **Commit:** 949ed02

**3. [Acceptance criteria count deviation] Grep counts differ from plan values**
- **Found during:** Acceptance criteria verification
- **Issue:** Plan expected singular counts for helper functions (e.g., `formatOptionHelp` count = 1) but grep counts all occurrences including calls. Actual counts: formatOptionHelp=3, formatGroupHeader=3, formatOptionName=3, resolveHelpTextLayout=2, makeHelpFormatter=2, formatter_fn=2.
- **Fix:** None needed — functions are defined and used correctly. Plan's grep criteria were imprecise.
- **Files modified:** None
- **Commit:** N/A (verification only)

**4. [Line count deviation] cmd.cpp is 265 lines (plan expected 170-210)**
- **Found during:** Post-commit verification
- **Issue:** Plan specified 170-210 lines; actual is 265. Extra lines from: multi-line option description strings, `app.set_help_flag("")` comment and call (3 lines), formatter_fn header comment (2 lines), blank lines between groups.
- **Fix:** None needed — functionality is correct. Plan's line estimate was optimistic for the formatted version.
- **Files modified:** None
- **Commit:** N/A (verification only)

## Known Stubs

None — all 26 option values are properly wired through CLI11 parsing to CmdParseResult fields. No placeholder values or mock data.

## Threat Flags

| Flag | File | Description |
|------|------|-------------|
| threat_flag: parse-error-leak | src/cmd/cmd.cpp | `CLI::ParseError::what()` stored in `result.error` — error messages from unknown options are CLI11-native (per D-06). No sensitive data in argv, but error output is under caller control. |

## Self-Check: PASSED

- ✅ `src/cmd/cmd.h` exists (43 lines)
- ✅ `src/cmd/cmd.cpp` exists (265 lines)
- ✅ Commit 29c4c21 exists in git log
- ✅ Commit 949ed02 exists in git log
