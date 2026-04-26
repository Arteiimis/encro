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

## Requirements

### Validated

- ✓ Compact progress mode (default single overall bar) — v1.0
- ✓ `--full-progress` flag restores per-worker/per-archive bars — v1.0
- ✓ Compact packing ("Packing: X/Y" single bar) — v1.0
- ✓ `--verbose-echo` correctly wins over `--full-progress` — v1.0
- ✓ Cross-subsystem `.compact` propagation in all PackPlan builders — v1.0

### Active

(To be defined in next milestone — run `/gsd-new-milestone`)

### Out of Scope

- GUI interface — CLI-first approach
- Cloud/remote encoding — local filesystem only
- Real-time encoding — batch processing focused

## Context

Shipped v1.0 with compact progress mode across all workflows (video encoding, picture compression, pack-only). 9 source files + 2 test files modified. 876 assertions across 203 test cases pass.

Tech stack: C++26, clang-cl, boost::program_options, libzippp, FFmpeg, Catch2, xmake.

## Key Decisions

| Decision | Outcome |
|----------|---------|
| Compact mode as default, `--full-progress` opt-in | ✓ Cleaner UX, less terminal noise |
| `compact = !ctx.config.fullProgress` pattern in both subsystems | ✓ Consistent flag semantics |
| All PackPlan builders explicitly set `.compact` | ✓ Defensive, prevents silent regression |
| 2-arg `packFilesToZip` no-progress overload for compact packing | ✓ Clean separation, no progress noise in compact mode |

## Known Issues / Tech Debt

- compress-picture path (`picture_process.cpp:467`) still has implicit `.compact` default
- Duplicate test case in `tests/pack_service_tests.cpp`
- No formal VERIFICATION.md artifacts for Phase 01 or Phase 02

---

*Last updated: 2026-04-26 after v1.0 milestone*
