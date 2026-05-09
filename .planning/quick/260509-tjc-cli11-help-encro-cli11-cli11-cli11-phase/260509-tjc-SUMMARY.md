---
phase: quick-260509-tjc
plan: 01
subsystem: CLI
tags: [cli11, help, description, formatter_fn, quick-task]
requires: [19-cli11-migration]
provides: [intro-line-in-cli11-help]
affects: [src/cmd/cmd.h, src/cmd/cmd.cpp, src/app/prelude.h, src/app/prelude.cpp, src/app/app_entry.cpp]
tech-stack:
  added: []
  patterns: [formatter_fn-description-prepend]
key-files:
  created: []
  modified:
    - src/cmd/cmd.h
    - src/cmd/cmd.cpp
    - src/app/prelude.h
    - src/app/prelude.cpp
    - src/app/app_entry.cpp
decisions:
  - "Intro line plumbed through commandLineInit → initStartup → CLI11 app.description() → formatter_fn"
  - "Pre-commit clang-format hook auto-staged all 5 files into single commit (plan called for 2 per-task commits)"
metrics:
  duration: 6.9min
  started: "2026-05-09T13:36:08Z"
  completed: "2026-05-09T13:43:00Z"
---

# Quick Task 260509-tjc: CLI11 Help Description Migration Summary

**One-liner:** Moved the `encro --help` app description line from a manually-colored `terminal::println(Heading, ...)` call into CLI11's help system via `app.description()` → custom `formatter_fn`.

## Plan Execution

**Plan:** 260509-tjc-PLAN.md — 2 auto tasks, fully autonomous, 0 checkpoints.

**Objective:** Remove premature ANSI color injection from the help intro line. The intro line now renders as plain text via CLI11's `app.description()` → `app_ptr->get_description()` in the custom `formatter_fn`. Phase 20 will add colors uniformly.

## Tasks Executed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | Accept intro line in commandLineInit, set app.description(), render in formatter_fn | f53ca6c | cmd.h, cmd.cpp |
| 2 | Thread intro line from app_entry through prelude; remove colored println | f53ca6c | prelude.h, prelude.cpp, app_entry.cpp |

> **Note:** The pre-commit clang-format hook auto-staged all 5 modified files into a single commit `f53ca6c`. Both tasks are captured in this one commit. A separate per-task commit per the plan was not possible due to the hook's staging behavior.

## Key Changes

1. **`cmd.h`** — `commandLineInit()` now accepts `std::string const& introLine` parameter.
2. **`cmd.cpp`** — Three changes:
   - `app.description(introLine)` stores the intro line in CLI11's app description.
   - `formatter_fn` lambda uses `app_ptr->get_description()` to prepend the description as plain text at the top of help output.
   - `app_ptr` parameter un-commented (was `/*app_ptr*/`) to access the description.
3. **`prelude.h`** — `initStartup()` now accepts `std::string const& introLine` parameter.
4. **`prelude.cpp`** — Forwards `introLine` to `commandLineInit(argc, argv, introLine)`.
5. **`app_entry.cpp`** — Three changes:
   - `run()` computes `helpIntroLine()` before `initStartup()` and passes it through.
   - `printHelp()` outputs only `cmd.helpText` — `terminal::println(Heading, ...)` call removed.
   - `terminal.h` include and `using enum terminal::MessageKind` preserved (still used by `failWithHint()`).

## Verification Results

All checks passed:

- ✅ `encro --help` starts with plain-text intro line: `encro: Universal video encoder/converter/packer | build: YYYY-MM-DD HH:MM:SS`
- ✅ No ANSI escape sequences in the intro line
- ✅ All four option groups render identically (General, Input/Output, Processing, File operation)
- ✅ Key options present (`--verbose`, `--input`, `--output`, `--type`, etc.)
- ✅ `encro` without `--help` produces no stdout output (error on stderr as before)
- ✅ Build compiles with zero warnings

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Pre-commit hook auto-staged all files]**
- **Found during:** Task 1 commit
- **Issue:** The pre-commit clang-format hook stashes unstaged changes, formats staged files, then restores. The restore step auto-staged all 5 modified files (Task 1 + Task 2) into a single commit instead of the planned 2 per-task commits.
- **Fix:** Accepted as-is — both tasks' changes are functionally correct in a single commit `f53ca6c`. The merge conflict (`UU` state) on cmd files was resolved via `git checkout --theirs` and re-commit.
- **Files affected:** All 5 files committed together.

### Out-of-Scope Findings

**1. `--help` flag not rendered in help output**
- The `-h,--help` flag is registered on the main `app` (not in any option group), and the custom `formatter_fn` only iterates over group options. This is a pre-existing issue from the CLI11 migration (Phase 19), not introduced by this task. Logged to `deferred-items.md`.

## Known Stubs

None — no TODOs, FIXMEs, placeholders, or unwired data sources found in modified files.

## Threat Flags

None — no new network endpoints, auth paths, file access patterns, or trust boundary changes introduced.

## Self-Check: PASSED

- ✅ `260509-tjc-SUMMARY.md` exists
- ✅ Commit `f53ca6c` exists in git log
- ✅ All 5 modified files exist on disk
- ✅ Build passes with zero warnings
- ✅ Help output verified correct
