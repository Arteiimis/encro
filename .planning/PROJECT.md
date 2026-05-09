# encro — Project Definition

**Project:** encro — CLI tool for video/picture encoding and zip packing via FFmpeg
**Language:** C++26 (clang-cl)
**Platform:** Windows primary, Linux/macOS supported

## What This Is

A fast, resumable CLI tool for batch video encoding and image compression with intelligent packing into zip archives. Pack subsystem has a single public entry point `pack::execute(PackRequest)` with zero internal type leakage to consumers. Compact progress bars by default with `--full-progress` for detailed per-worker/per-archive display.

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

## Context

Shipped v1.0 (compact progress mode), v1.1 (lambda readability refactor), v1.2 (tech debt & code quality), v1.3 (pack subsystem OO refactor), v1.4 (PackRequest declarative API & IPacker removal), and v1.5 (Pack下沉收尾 — 消除调用方泄漏).

v1.5 pack subsystem architecture (shipped):
- `pack.h`: single public header — PackRequest, PackMode, NamingConfig, NamingStrategy, GroupingStrategy, SummaryConfig, execute() declaration
- `pack_types.h`: public types — PackFileEntry, PackEntryInput, FileOrdinalRange
- `pack_plan_internal.h`: internal-only — PackPlan struct, execute(PackPlan) declaration
- Packer: zip I/O, grouping, file copy — internal-only, direct value semantics
- PackService: orchestration, owns Packer by value, internal executor
- `pack::execute()`: free function entry point, all grouping/naming/Plan construction internal
- `pack::internal::` namespace: demoted static helpers, detail types, internal constants
- 3 consumers (pipeline/video/picture) use pack::execute(PackRequest) exclusively — zero internal pack type includes
- NamingStrategy enum (Flat/FlatWithForce/Keep) replaces OutputLayout+boolean pair
- GroupingStrategy enum (PerSourceDir/PerSourceDirKeepTogether) on PackRequest
- SummaryConfig with isSummary structural flag replaces "0000__" prefix convention
- PackPlan fully internalized — compile-time enforced boundary via __if_exists

Tech stack: C++26, clang-cl, boost::program_options, libzippp, FFmpeg, Catch2, xmake.
157 assertions pass (packer + pack-service integration tests), 945 total E2E assertions preserved.

## Current State

**Shipped:** v1.0 through v1.5. All milestones complete.

Pack subsystem:
- PackRequest declarative single-entry API: `pack::execute(request)` — consumers describe intent, all orchestration internal
- Naming internalized: `NamingConfig` with `NamingStrategy` enum (Flat/FlatWithForce/Keep), no call-site naming logic
- Grouping declarative: `GroupingStrategy` enum (PerSourceDir/PerSourceDirKeepTogether) on PackRequest
- Summary structural: `SummaryConfig` with `isSummary` flag, replaces "0000__" string prefix convention
- Configuration centralized: compact from `AppConfig.fullProgress`, all CLI params injected via PackRequest fields
- No abstract layer: Packer used directly by value, IPacker/MockPacker deleted
- PackPlan fully internalized: moved to `pack_plan_internal.h`, unreachable from public `pack.h`
- Architecture: Packer (I/O) → PackService (orchestration, owns Packer) → `pack::execute()` (free function entry point)

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

**v1.4 Pack 接口简化 & 抽象层清理:**
- ✓ SIMPLIFY-01~04: PackRequest 声明式单一入口 API — PackPlan 不再对外暴露 — v1.4
- ✓ SIMPLIFY-05~08: 内部分组统一 + 命名内化 — 两层切分，命名规则收归模块内部 — v1.4
- ✓ SIMPLIFY-09~10: 配置集中注入 — compact 从 AppConfig 推导，修复 picture 硬编码 — v1.4
- ✓ SIMPLIFY-11,13~14: 945 assertions 零行为变化，恢复性执行 + 冲突处理逻辑不变 — v1.4
- ✓ SIMPLIFY-15~17: 移除 IPacker + MockPacker — PackService 直接持有 Packer 值 — v1.4

**v1.5 Pack下沉收尾 — 消除调用方泄漏:**
- ✓ SINK-01: NamingStrategy 枚举 (Flat/FlatWithForce/Keep) 替换 OutputLayout+boolean 对 — v1.5
- ✓ SINK-02: GroupingStrategy 枚举 + SummaryConfig 结构体，isSummary 标志位替换 "0000__" 前缀 — v1.5
- ✓ SINK-03: picture_process.cpp 移除 3 个内部 pack include，零内部类型泄漏 — v1.5
- ✓ SINK-04: PackPlan 移入 pack_plan_internal.h，编译期边界强制 via `__if_exists` — v1.5
- ✓ 3033 assertions across 244 test cases pass with zero behavioral regression — v1.5

## Current Milestone: v1.6 CLI体验增强

**Goal:** 现代化 CLI 选项解析并统一终端着色体验

**Target features:**
- CLI11 迁移 — boost::program_options → CLI11，26个 option，colored --help
- CLI着色深化 — 将 terminal:: 语义着色推广到 errors、warnings、--version（progress bar 待定）

**Key context:**
- 零新依赖着色 — 复用现有 `fmtlib color` + `terminal::` 语义层
- ~138行 cmd.cpp 重写 + ~30处 vm 调用适配
- xmake.lua / boost 依赖由用户自行处理
- Progress bar 着色归属不确定 — progress bar lib 可能有自有颜色体系，需单独评估
- 所有行为回归由现有 3033 assertions 守卫

### Out of Scope

- GUI interface — CLI-first approach
- Cloud/remote encoding — local filesystem only
- Real-time encoding — batch processing focused
- C++20 modules — clang-cl module support not production-ready for MSVC-ABI targets
- DI framework (Boost.DI, etc.) — constructor injection sufficient
- Getter/setter for every data field — anti-pattern per C++ Core Guidelines
- PackPlan → class with private data — 16 designated-initializer sites preserved as aggregate

### Architecture (current v1.5)

- `pack.h`: single public header — PackRequest, PackMode, NamingConfig, NamingStrategy, GroupingStrategy, SummaryConfig, execute() declaration
- `pack_types.h`: public types — PackFileEntry, PackEntryInput, FileOrdinalRange
- `pack_plan_internal.h`: internal-only — PackPlan struct, execute(PackPlan) declaration
- Packer: zip I/O, grouping, file copy — internal-only, direct value semantics, no abstract base
- PackService: orchestration, owns Packer by value, internal executor
- `pack::execute()`: free function entry point, all grouping/naming/Plan construction internal
- `pack::internal::` namespace: demoted static helpers, detail types, internal constants
- IPacker / MockPacker: deleted — mock tests rewritten as real Packer + TempDir integration tests
- 3 consumers (pipeline/video/picture) use pack::execute(PackRequest) exclusively — zero internal pack type includes
- All legacy patterns preserved: compact progress, resumability, conflict handling

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
| [Phase 12 D-01~14]: PackRequest declarative API — PackMode enum, NamingConfig, NamingMode | ✓ 3 consumers migrated, archive_plan.cpp deleted, zero behavioral regression |
| [Phase 12 D-02]: pack::execute() free function as single public entry point | ✓ Consumers describe intent, all orchestration internal |
| [Phase 12 D-04]: PackService static methods demoted to pack::internal namespace | ✓ Clean public API surface — only PackRequest types in pack.h |
| [Phase 13 D-04]: 1个 callback parameter per entry site vs callback map lookups | ✓ No map overhead, simple pointer-based dispatch |
| [Phase 13 D-08]: picture_process non-compress path uses pack::execute() directly | ✓ Removes buildPicturePackPlan(), eliminates single/double-layer fork |
| [Phase 14 D-01]: IPacker abstract base deleted — Packer no longer inherits anything | ✓ Zero virtual dispatch, simpler lifecycle, cleaner code |
| [Phase 14 D-02]: PackService holds Packer by value, no unique_ptr indirection | ✓ Direct construction in stack scope, no heap allocation |
| [Phase 14 D-03]: MockPacker deleted, tests rewritten as real Packer integration tests | ✓ Real I/O coverage with TempDir, 56 packer + 70 pack-service assertions |
| [Phase 15]: Single NamingStrategy enum (Flat/FlatWithForce/Keep) adopted — two-axis model rejected | ✓ Represents invalid state combination `{Keep, forceConflictHandling=true}` |
| [Phase 16]: Summary entry ordering via `bool isSummary` flag, not string prefix convention | ✓ Structurally enforced, not fragile lexicographic ordering |
| [Phase 16]: GroupingStrategy::PerSourceDirKeepTogether is SEMANTIC not mechanical | ✓ Prevents two-layer partitioning leak, threshold=0 rejected |
| [Phase 17]: ~200 lines dead code deleted from picture_process.cpp — PackRequest eliminates 14 functions | ✓ -565 lines in picture_process.cpp, -228 net across project |
| [Phase 18]: __if_exists (MSVC/clang extension) for compile-boundary test — SFINAE cannot detect namespaces | ✓ PackPlan unreachable from pack.h, compile-time enforced |

## Known Issues / Tech Debt

None — all v1.0–v1.5 items resolved or intentionally dropped.

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

*Last updated: 2026-05-05 after v1.5 milestone completion*
