---
phase: 260519-1ym
plan: "01"
subsystem: cmd/app
tags: [dry-run, validation, scan, plan, job-state, CLI]
requires: []
provides: [--dry-run flag, pipeline::runDryRun, three-layer preview output]
affects: [cmd parsing, app entry routing, pipeline namespace]
tech-stack:
  added: []
  patterns: [progressive-layered-output, read-only-state-access, fail-fast-validation]
key-files:
  created:
    - src/app/dry_run.cpp
    - tests/dry_run_flag_test.cpp
    - tests/app/dry_run_test.cpp
  modified:
    - src/cmd/cmd.h
    - src/cmd/cmd.cpp
    - src/core/app_context.h
    - src/cmd/config_builder.cpp
    - src/app/app_entry.cpp
    - src/app/pipeline.h
decisions:
  - "--dry-run is a General group bool flag (no short form -d)"
  - "Three-layer progressive output: Validation -> Scan -> Plan"
  - "Each layer failure skips subsequent layers (fail-fast)"
  - "Read-only job state via detail::loadSnapshot, never Store::initialize()"
  - "Output dir check only — never create directories"
  - "Full recursive scan by media type extensions"
metrics:
  duration: "~25 minutes"
  completed: "2026-05-19T01:50:00Z"
---

# Phase 260519-1ym Plan 01: --dry-run Implementation Summary

**One-liner:** Implement `--dry-run` CLI flag with three-layer progressive output (Validation, Scan, Plan) for pre-flight visibility into encro operations without side effects.

## Tasks Executed

| # | Type | Name | Commit | Status |
|---|------|------|--------|--------|
| 1 | TDD-RED | Failing test for --dry-run flag registration | `6d866a3` | Done |
| 1 | TDD-GREEN | Register --dry-run flag, wire through AppConfig to routing | `b4bc9a0` | Done |
| 2 | TDD-RED | Failing tests for three-layer dry-run pipeline | `c4675b9` | Done |
| 2 | TDD-GREEN | Implement pipeline::runDryRun with validation, scan, plan | `9d27b9a` | Done |

## Verification Results

- **Build:** `xmake build encro` and `xmake build tests` pass
- **Tests:** 3173 assertions in 293 test cases — all pass
- **Smoke test:** `encro --dry-run -i .` prints three-layer output with semantic coloring
- **Help output:** `--dry-run` appears under General options group
- **Side effects:** No output directories or job state files created during dry-run

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed terminal::println() no-argument calls**
- **Found during:** Task 2 implementation
- **Issue:** `terminal::println()` with no arguments does not compile — the template requires at least a MessageKind and format string
- **Fix:** Replaced all `terminal::println()` blank-line calls with `terminal::println(Plain, "")`
- **Files modified:** `src/app/dry_run.cpp`
- **Commit:** `9d27b9a`

**2. [Rule 3 - Blocking] Test file location for xmake build resolution**
- **Found during:** Task 1 RED phase
- **Issue:** xmake resolves `tests/*.cpp` relative to parent project directory, not the worktree. New test files must be copied to the parent project's tests directory for xmake to find them.
- **Fix:** Test files are created in the worktree (for git tracking) and copied to the parent project's tests directory for building.
- **Files modified:** Build workflow (no code changes)

**3. [Rule 1 - Bug] AppContext non-copyable in test helpers**
- **Found during:** Task 2 RED phase
- **Issue:** `AppContext` has a deleted copy constructor due to `immer::atom` in `VideoInfoCacheStore`. Helper function `makeContext()` could not return by value.
- **Fix:** Changed test helpers to use `void setup*(AppContext&, ...)` taking a mutable reference instead of returning by value.
- **Files modified:** `tests/app/dry_run_test.cpp`
- **Commit:** `c4675b9`

## TDD Gate Compliance

- RED gate 1: `6d866a3` — `test(260519-1ym): add failing test for --dry-run flag registration and wiring` (compilation failure before implementation)
- GREEN gate 1: `b4bc9a0` — `feat(260519-1ym): register --dry-run CLI flag with AppConfig and pipeline routing`
- RED gate 2: `c4675b9` — `test(260519-1ym): add failing tests for three-layer dry-run pipeline` (2 tests failed with stub)
- GREEN gate 2: `9d27b9a` — `feat(260519-1ym): implement three-layer dry-run pipeline with validation, scan, and plan`
- REFACTOR: Not needed — implementation is clean and minimal

## Threat Flags

None — all threat dispositions from the plan's `<threat_model>` are satisfied:
- T-dry-02 (Tampering/job state): Mitigated via read-only `detail::loadSnapshot`, no `Store::initialize()` call
- All other threats accepted as low risk per plan analysis

## Known Stubs

None — all data flows are wired:
- Validation checks ffmpeg/ffprobe paths (from resolved toolchain), input existence, output parent permissions
- Scan performs full recursive `media::scanByExtensions()` with real file sizes
- Plan computes encode count, format, workers, and pack archive estimate from real scan data
- Job state resume reads from `detail::loadSnapshot()` and displays real task counts

## Self-Check: PASSED

- `src/app/dry_run.cpp`: EXISTS (294 lines)
- `tests/dry_run_flag_test.cpp`: EXISTS
- `tests/app/dry_run_test.cpp`: EXISTS
- Commits `6d866a3`, `b4bc9a0`, `c4675b9`, `9d27b9a`: VERIFIED in git log
- `--dry-run` in help output: VERIFIED
- Three-layer output: VERIFIED via smoke test
