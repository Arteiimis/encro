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

## Current Milestone: v1.3 Pack Subsystem OO Refactor

**Goal:** 将 pack 子系统及相关核心模块重构为面向对象风格，以程序关键路径和流程为导向抽象封装，模块独立、可测试性强

**Target features:**
- Pack 核心 struct → class 封装（pack_service, packer 等），数据成员私有化
- 自由函数归入类方法，公共接口清晰，实现细节隐藏
- 紧密耦合的核心/共享模块同步改造
- 模块可独立编译与测试，支持 Mock 外部依赖
- 测试覆盖不退化（909 assertions 维持）

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

### Active

<!-- Current scope for v1.3. Building toward these. -->

- [ ] Pack 核心 struct 封装为 class，数据成员私有，提供明确公共接口
- [ ] 自由函数（pack_service.cpp, packer.cpp 等）归入相应类方法
- [ ] 紧密耦合的核心模块同步改造（共享数据结构、工具函数等）
- [ ] 模块可独立编译与单元测试，支持 Mock 外部依赖
- [ ] 所有现有测试保持通过（909 assertions, 215 test cases）

### Out of Scope

- GUI interface — CLI-first approach
- Cloud/remote encoding — local filesystem only
- Real-time encoding — batch processing focused

## Context

Shipped v1.0 (compact progress mode), v1.1 (lambda readability refactor), and v1.2 (tech debt & code quality). v1.2 resolved all 3 known gaps from prior milestones: explicit `.compact = true` in all PackPlan sites, removed duplicate assertion, backfilled Phase 01/02 VERIFICATION.md. `video_batch_execution.cpp` split into 2 compilation units with zero behavioral change. 909 assertions across 215 test cases pass.

Tech stack: C++26, clang-cl, boost::program_options, libzippp, FFmpeg, Catch2, xmake.

## Current State

v1.0, v1.1, v1.2 shipped. v1.3 started — Pack Subsystem OO Refactor. Codebase is clean: 0 deeply nested lambdas, 0 implicit struct defaults in PackPlan, 0 duplicate assertions. STRUCT-01 (template relocation) confirmed unnecessary. Beginning gradual OO transformation, starting with pack subsystem and coupled core modules.

### Architecture

- 10 extracted free functions in anonymous namespaces across 4 `.cpp` files (v1.1)
- Factory function pattern for lambda-wrapping-lambda (pack_service.cpp) (v1.1)
- 1-line jthread delegation pattern for monitor/spinner loops (v1.1)
- Individual typed parameters for captured variables — no context structs (v1.1)
- Split compilation units: `video_encoding_state.cpp` + `video_batch_execution.cpp` (v1.2)
- `videobatch::detail` namespace in header for shared struct definitions (v1.2)
- All PackPlan sites explicitly set `.compact = true` with `static_assert` guard (v1.2)

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
| `static_assert(std::is_aggregate_v<pack::PackPlan>)` guard | ✓ Catches accidental non-aggregate breakage of `.compact` |
| STRUCT-02 split boundary: state/monitor vs. task execution | ✓ 2 independently-compiling TUs, zero behavioral change |
| STRUCT-01 cancelled: templates already correctly placed | ✓ Avoided unnecessary `core/` header creation |
| `videobatch::detail` namespace for shared struct definitions | ✓ Required for cross-TU value semantics (narrow D-01 exception) |

## Known Issues / Tech Debt

All v1.0 deferred items resolved in v1.2:
- ~~compress-picture path implicit .compact default~~ → Resolved by DEBT-01 (explicit `.compact = true` at all 4 sites)
- ~~Duplicate test case in pack_service_tests.cpp~~ → Resolved by DEBT-02 (redundant assertion removed)
- ~~VERIFICATION.md missing for Phase 01, 02~~ → Resolved by PROC-01 (backfilled)

Remaining items:
- No formal milestone audit file (v1.2-MILESTONE-AUDIT.md) — deferred for speed
- 6 quick tasks missing formal status files — acknowledged at v1.2 close (see STATE.md Deferred Items)
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

*Last updated: 2026-04-29 — v1.3 Pack Subsystem OO Refactor milestone started*
