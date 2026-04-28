# encro — Project Definition

**Project:** encro — CLI tool for video/picture encoding and zip packing via FFmpeg
**Language:** C++26 (clang-cl)
**Platform:** Windows primary, Linux/macOS supported

## What This Is

A fast, resumable CLI tool for batch video encoding and image compression with intelligent packing into zip archives. Compact progress bars by default with `--full-progress` for detailed per-worker/per-archive display.

## Core Value

Progress visibility: users always see what's happening with minimal terminal noise. Compact single-bar progress is the default.

## Vision

Users run a single command (`encro -i <path> --pack`) to encode and pack entire directories with clear progress feedback.

## Principles

- CLI-first: everything driven by command-line flags
- Progress visibility: compact single-bar progress by default, detailed mode opt-in
- Resumability: interrupted jobs can continue via persistent state
- No data loss: errors handled explicitly, nothing deleted silently
- Code clarity: no deeply nested lambdas, inline lambdas kept short and readable

## Requirements

### Validated

**v1.0 Compact Progress Mode:**
- ✓ Compact progress mode (default single overall bar) — v1.0
- ✓ `--full-progress` flag restores per-worker/per-archive bars — v1.0
- ✓ Compact packing ("Packing: X/Y" single bar) — v1.0
- ✓ `--verbose-echo` correctly wins over `--full-progress` — v1.0
- ✓ Cross-subsystem `.compact` propagation in all PackPlan builders — v1.0

**v1.1 Lambda Readability Refactor:**
- ✓ 4 deeply nested lambdas (3+ levels) in video_batch_execution.cpp extracted to named functions — v1.1
- ✓ 4 multiline/inline lambdas in pack_service.cpp + packer.cpp extracted — v1.1
- ✓ 2 named lambda variables in picture_process.cpp extracted to free functions — v1.1
- ✓ All 910 assertions across 215 test cases pass unchanged — v1.1
- ✓ Milestone audit PASSED — 10 functions extracted, 0 header file modifications

### Active (v1.2 Tech Debt & Code Quality)

- [ ] **DEBT-01**: Fix implicit `.compact` default in `picture_process.cpp:467` — explicit `.compact = true` in `buildPicturePackPlan`
- [ ] **DEBT-02**: Remove duplicate test case `selectPackPlanIndexes preserves compact` in `tests/pack_service_tests.cpp:131-168`
- [ ] **DEBT-03**: Backfill VERIFICATION.md for Phase 01 (Compact Progress Mode) and Phase 02 (Compact Mode Gap Fixes)
- [ ] **OPTIM-01**: Refactor `withActionJobState`/`withJobState` shared template helpers — reduce duplication across video/picture subsystems
- [ ] **OPTIM-02**: Split `video_batch_execution.cpp` (765 lines) into smaller compilation units — improve modularity and compile times

### Out of Scope

- GUI interface — CLI-first approach
- Cloud/remote encoding — local filesystem only
- Real-time encoding — batch processing focused

## Context

Shipped v1.0 with compact progress mode across all workflows (video encoding, picture compression, pack-only). Shipped v1.1 Lambda Readability Refactor — 10 lambda functions extracted to named free functions across 4 source files, 0 header modifications, 910 assertions across 215 test cases pass.

Tech stack: C++26, clang-cl, boost::program_options, libzippp, FFmpeg, Catch2, xmake.

## Current State

v1.2 Tech Debt & Code Quality milestone started. Both v1.0 and v1.1 milestones are shipped. Sources comply with code clarity principle: 0 deeply nested lambdas. Focus: fix implicit defaults, remove dead code, backfill process artifacts, refactor shared templates, reduce file sizes.

### Architecture

- 10 extracted free functions in anonymous namespaces across 4 `.cpp` files
- Factory function pattern for lambda-wrapping-lambda (pack_service.cpp)
- 1-line jthread delegation pattern for monitor/spinner loops (video_batch_execution.cpp, packer.cpp)
- Individual typed parameters for captured variables (no context structs)

## Key Decisions

| Decision | Outcome |
|----------|---------|
| Compact mode as default, `--full-progress` opt-in | ✓ Cleaner UX, less terminal noise |
| `compact = !ctx.config.fullProgress` pattern in both subsystems | ✓ Consistent flag semantics |
| All PackPlan builders explicitly set `.compact` | ✓ Defensive, prevents silent regression |
| 2-arg `packFilesToZip` no-progress overload for compact packing | ✓ Clean separation, no progress noise in compact mode |
| D-01: Free functions in anonymous namespace for lambda extraction | ✓ 10 functions extracted, 0 header modifications |
| D-02: Individual typed parameters for captured variables | ✓ Consistent across all phases, max 7 params (packSourceEntryChunks) |
| D-03: 2-level lambda nesting acceptable, only 3+ targeted | ✓ Boundary respected, over-extraction avoided |
| Factory function pattern for lambda-wrapping-lambda (Phase 4) | ✓ Designated initializer assignment, clean call sites |
| TDD RED gate cycle for higher-risk extractions | ✓ 6 RED→GREEN cycles across Phases 3-5 |
| 1-line jthread delegation pattern for monitor/spinner loops | ✓ 3 jthread sites with clean 1-line lambdas |

## Known Issues / Tech Debt

- compress-picture path (`picture_process.cpp:467`) still has implicit `.compact` default
- Duplicate test case in `tests/pack_service_tests.cpp`
- No formal VERIFICATION.md artifacts for Phase 01 or Phase 02

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd-transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd-complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---

*Last updated: 2026-04-28 — v1.2 Tech Debt & Code Quality milestone started*
