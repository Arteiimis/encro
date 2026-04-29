# encro — Project Definition

**Project:** encro — CLI tool for video/picture encoding and zip packing via FFmpeg
**Language:** C++26 (clang-cl)
**Platform:** Windows primary, Linux/macOS supported

## What This Is

A fast, resumable CLI tool for batch video encoding and image compression with intelligent packing into zip archives. Pack subsystem is fully object-oriented with injectable dependencies and mock-based test boundaries. Compact progress bars by default with `--full-progress` for detailed per-worker/per-archive display.

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
- Encapsulation: subsystems as classes with clear public interfaces, implementation hidden in `.cpp`
- Testability: injectable dependencies via abstract interfaces, mock-based unit tests
- Zero hot-path overhead: virtual dispatch only at archive/operation granularity

## Current Focus

v1.0–v1.3 shipped. Planning next milestone.

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

**v1.2 Tech Debt & Code Quality:**
- ✓ DEBT-01: Fixed implicit `.compact` default — all 4 PackPlan sites now explicitly set `.compact = true` — v1.2
- ✓ DEBT-02: Removed duplicate assertion `CHECK(result.compact == true)` at `pack_service_tests.cpp:161` — v1.2
- ✓ PROC-01: Backfilled structured VERIFICATION.md for Phase 01 and Phase 02 — v1.2
- ✓ STRUCT-02: Split `video_batch_execution.cpp` (804 lines) into 2 compilation units — v1.2
- ✓ STRUCT-01: Cancelled — template helpers already correctly placed in `video_workflow_utils.h` — v1.2
- ✓ All 909 assertions across 215 test cases pass unchanged — v1.2

**v1.3 Pack Subsystem OO Refactor:**
- ✓ TYPE-01~04: `pack_types.h` + `packer_types.h` created, circular dependency broken — v1.3
- ✓ SVC-01~08: Packer + PackService final classes, 30+ free functions consolidated — v1.3
- ✓ PackProgressCallbacks sub-struct: 5 callbacks → 1 field on PackPlan — v1.3
- ✓ DI-01~06: IPacker interface, MockPacker, `unique_ptr<IPacker>` DI, ZipWriter RAII — v1.3
- ✓ MIG-01~05: 7 consumers migrated to OO API, `pack_facade.h` (248 lines) deleted — v1.3
- ✓ 945 assertions across 225 test cases pass (909 baseline + 36 mock) — v1.3

### Active

- [ ] Promote CompactProgressState to public class if needed by other subsystems
- [ ] Template-based pack format abstraction (tar/7z) — future consideration
- [ ] E2E CLI verification for v1.3 deferred paths — requires test media + FFmpeg

### Out of Scope

- GUI interface — CLI-first approach
- Cloud/remote encoding — local filesystem only
- Real-time encoding — batch processing focused
- C++20 modules — clang-cl module support not production-ready for MSVC-ABI targets
- DI framework (Boost.DI, etc.) — constructor injection sufficient
- Getter/setter for every data field — anti-pattern per C++ Core Guidelines
- PackPlan → class with private data — 16 designated-initializer sites preserved as aggregate

## Context

Shipped v1.0 (compact progress mode), v1.1 (lambda readability refactor), v1.2 (tech debt & code quality), and v1.3 (pack subsystem OO refactor). All 4 milestones complete. Codebase is clean: 0 deeply nested lambdas, 0 implicit struct defaults, 0 duplicate assertions, 0 free functions in pack subsystem, 0 global-scope pack types.

Pack subsystem architecture (v1.3):
- `pack_types.h` / `packer_types.h`: shared value types, no circular dependencies
- `IPacker`: abstract interface (3 virtual methods, archive granularity)
- `Packer final : IPacker`: production zip I/O via libzippp, ZipWriter RAII
- `PackService final`: orchestration with `unique_ptr<IPacker>` constructor injection
- `MockPacker : IPacker`: capture-recording test double for unit tests
- `PackProgressCallbacks`: callback sub-struct on PackPlan aggregate

Tech stack: C++26, clang-cl, boost::program_options, libzippp, FFmpeg, Catch2, xmake.
945 assertions across 225 test cases, 0 failures.

## Current State

v1.0, v1.1, v1.2, v1.3 all shipped. Pack subsystem fully OO with DI and mock boundaries. Codebase ready for next milestone.

### Architecture

- OO pack subsystem: Packer, PackService, IPacker, MockPacker — encapsulated, injectable, testable
- Factory function pattern for lambda-wrapping-lambda (pack_service.cpp) (v1.1)
- 1-line jthread delegation pattern for monitor/spinner loops (v1.1)
- Individual typed parameters for captured variables — no context structs (v1.1)
- Split compilation units: `video_encoding_state.cpp` + `video_batch_execution.cpp` (v1.2)
- `videobatch::detail` / `pack::detail::` namespace for internal types (v1.2/v1.3)
- All PackPlan sites explicitly set `.compact = true` with `static_assert` guard (v1.2)
- PackPlan aggregate preserved: all 16 designated-initializer sites intact (v1.3)
- All pack classes `final`, method bodies in `.cpp`, zero virtual in hot path (v1.3)

## Key Decisions

| Decision | Outcome |
|----------|---------|
| Compact mode as default, `--full-progress` opt-in | ✓ Cleaner UX, less terminal noise |
| `compact = !ctx.config.fullProgress` pattern in both subsystems | ✓ Consistent flag semantics |
| All PackPlan builders explicitly set `.compact` | ✓ Defensive, prevents silent regression |
| 2-arg `packFilesToZip` no-progress overload for compact packing | ✓ Clean separation, no progress noise in compact mode |
| D-01: Free functions in anonymous namespace for lambda extraction | ✓ 10 functions extracted, 0 header modifications |
| D-02: Individual typed parameters for captured variables | ✓ Consistent across all phases |
| D-03: 2-level lambda nesting acceptable, only 3+ targeted | ✓ Boundary respected, over-extraction avoided |
| Factory function pattern for lambda-wrapping-lambda (Phase 4) | ✓ Designated initializer assignment, clean call sites |
| TDD RED gate cycle for higher-risk extractions | ✓ 6 RED→GREEN cycles across Phases 3-5 |
| 1-line jthread delegation pattern for monitor/spinner loops | ✓ 3 jthread sites with clean 1-line lambdas |
| `static_assert(std::is_aggregate_v<pack::PackPlan>)` guard | ✓ Catches accidental non-aggregate breakage |
| STRUCT-02 split boundary: state/monitor vs. task execution | ✓ 2 independently-compiling TUs, zero behavioral change |
| STRUCT-01 cancelled: templates already correctly placed | ✓ Avoided unnecessary `core/` header creation |
| `videobatch::detail` namespace for shared struct definitions | ✓ Required for cross-TU value semantics |
| [Phase 8 D-01/D-02]: pack_types.h + packer_types.h split; PackPlan in pack_types.h | ✓ Circular dependency broken, includes clean |
| [Phase 9 D-01]: Packer = zip I/O + grouping; PackService = orchestration | ✓ Clear responsibility, single-axis classes |
| [Phase 9 D-02]: PackProgressCallbacks sub-struct (5 → 1 field) | ✓ Cleaner PackPlan, designated initializers with sub-aggregates |
| [Phase 9 D-04]: pack_facade.h as temporary backward-compat bridge | ✓ Zero consumer call-site changes, removed in Phase 11 |
| [Phase 10 D-01]: IPacker 3-method interface at archive granularity | ✓ Mock boundary without hot-path overhead |
| [Phase 10 D-03]: MockPacker capture-recording design (no framework) | ✓ Straightforward test assertions, no mock library |
| [Phase 10 D-05]: Additive testing — mock tests + preserved integration tests | ✓ Dual-layer coverage, no coverage loss risk |
| [Phase 11 D-01]: All-at-once consumer migration, single commit | ✓ No intermediate mixed-API states |
| [Phase 11 D-03]: Per-call-site stack instances for PackService/Packer | ✓ Simple lifecycle, no shared state, no function signature changes |

## Known Issues / Tech Debt

All v1.0–v1.2 deferred items resolved.
Remaining items:
- 6 quick tasks missing formal status files (acknowledged at v1.2/v1.3 close — see STATE.md Deferred Items)
- E2E CLI verification for v1.3 deferred (1 of 6 Phase 11 requirements) — requires test media + FFmpeg
- `noteStopRequest` and `truncateForProgressLabel` intentionally duplicated across both TU anonymous namespaces — standard C++ pattern, not debt

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

*Last updated: 2026-04-30 after v1.3 Pack Subsystem OO Refactor milestone*
