# encro — Roadmap

## Milestones

- ✅ **v1.0 Compact Progress Mode** — Phases 1-2 (shipped 2026-04-26)
- ✅ **v1.1 Lambda Readability Refactor** — Phases 3-5 (shipped 2026-04-27)
- ✅ **v1.2 Tech Debt & Code Quality** — Phases 6-7 (shipped 2026-04-29)
- ✅ **v1.3 Pack Subsystem OO Refactor** — Phases 8-11 (shipped 2026-04-30)
- ✅ **v1.4 Pack 接口简化 & 抽象层清理** — Phases 12-14 (shipped 2026-05-01)
- ✅ **v1.5 Pack下沉收尾 — 消除调用方泄漏** — Phases 15-18 (shipped 2026-05-04)
- ✅ **v1.6 CLI体验增强** — Phases 19-20 (shipped 2026-05-09)

## Phases

<details>
<summary>✅ v1.0 Compact Progress Mode (Phases 1-2) — SHIPPED 2026-04-26</summary>

- [x] Phase 1: Compact Progress Mode (2/2 plans) — completed 2026-04-26
- [x] Phase 2: Compact Mode Gap Fixes (1/1 plan) — completed 2026-04-26

</details>

<details>
<summary>✅ v1.1 Lambda Readability Refactor (Phases 3-5) — SHIPPED 2026-04-27</summary>

- [x] Phase 3: Video Subsystem Refactor (2/2 plans) — completed 2026-04-27
- [x] Phase 4: Pack Subsystem Refactor (2/2 plans) — completed 2026-04-27
- [x] Phase 5: Picture Refactor + Final Validation (3/3 plans) — completed 2026-04-27

</details>

<details>
<summary>✅ v1.2 Tech Debt & Code Quality (Phases 6-7) — SHIPPED 2026-04-29</summary>

- [x] Phase 6: Must-Fix Debt (3/3 plans) — completed 2026-04-28
  - DEBT-01: Fix implicit `.compact` default
  - DEBT-02: Remove redundant assertion
  - PROC-01: Backfill VERIFICATION.md
- [x] Phase 7: Structural Optimization (1/1 plan) — completed 2026-04-29
  - STRUCT-02: Split video_batch_execution.cpp (STRUCT-01 cancelled)

</details>

<details>
<summary>✅ v1.3 Pack Subsystem OO Refactor (Phases 8-11) — SHIPPED 2026-04-30</summary>

- [x] Phase 8: Type Extraction & Namespace Cleanup (1/1 plan) — completed 2026-04-29
  - TYPE: pack_types.h, packer_types.h, circular dependency broken
- [x] Phase 9: Service Class Extraction (4/4 plans) — completed 2026-04-29
  - SVC: Packer + PackService final classes, PackProgressCallbacks, pack_facade.h
- [x] Phase 10: Dependency Injection & Testability (5/5 plans) — completed 2026-04-30
  - DI: IPacker interface, MockPacker, DI constructor, ZipWriter RAII, 36 new assertions
- [x] Phase 11: Consumer Migration & Cleanup (1/1 plan) — completed 2026-04-30
  - MIG: 7 consumers → OO API, pack_facade.h deleted, 945 assertions pass

</details>

<details>
<summary>✅ v1.4 Pack 接口简化 & 抽象层清理 (Phases 12-14) — SHIPPED 2026-05-01</summary>

- [x] Phase 12: PackRequest 声明式 API & 配置注入 — completed 2026-05-01 (4/4 plans)
- [x] Phase 13: 分组统一 & 命名内化 — complete (4/4 plans)
- [x] Phase 14: 移除 IPacker 抽象层 & 验证 — complete (1/1 plan)

</details>

### ✅ v1.5 Pack下沉收尾 — 消除调用方泄漏

**Milestone Goal:** 彻底消除 picture_process.cpp 对 pack 内部类型的依赖，所有调用方统一通过 `pack::execute(PackRequest)` 交互。命名冲突处理抽象为策略枚举 + 前缀配置，PackRequest API 扩展 summary/分组策略字段，PackPlan 退化为纯内部类型。

- [x] **Phase 15: Naming Strategy Enum + NamingConfig Migration** — `NamingStrategy` enum replaces `OutputLayout`+boolean pair; internal dispatch only (2/2 plans) (completed 2026-05-04)
- [x] **Phase 16: Grouping Strategy + Summary Config on PackRequest** — `GroupingStrategy` enum + `SummaryConfig` struct; picture's two-layer partitioning declarative (2/2 plans) (completed 2026-05-04)
- [x] **Phase 17: Picture Process Leak Elimination** — `picture_process.cpp` replaces `buildPicturePackPlan()` with `pack::execute(PackRequest)` (completed 2026-05-04)
- [x] **Phase 18: PackPlan Pure Internalization** — `PackPlan` moved to internal header; compile-time enforcement of consumer invisibility (completed 2026-05-04)
<details>
<summary>✅ v1.6 CLI体验增强 (Phases 19-20) — SHIPPED 2026-05-09</summary>

**Milestone Goal:** CLI11 替换 boost::program_options，终端输出全面语义着色（--help / --version / errors），NO_COLOR 标准合规。

- [x] Phase 19: CLI11 Migration (5/5 plans) — completed 2026-05-09
  - CLI11 replaces boost::po; project builds, 26 options parse, 3033 assertions pass, zero user-visible change
- [x] Phase 20: CLI Color Deepening (3/3 plans) — completed 2026-05-09
  - MessageKind extended, formatter_fn colored --help, --version colored, errors via terminal::println, NO_COLOR compliance

</details>
## Phase Details

### Phase 15: Naming Strategy Enum + NamingConfig Migration
**Goal**: Naming conflict resolution is abstracted behind a single `NamingStrategy` enum, eliminating the brittle `OutputLayout`+`forceConflictHandling` boolean pair. Consumers declare intent via one enum value; internal dispatch uses single-switch.

**Depends on**: Phase 14 (v1.4 baseline)
**Requirements**: SINK-01
**Success Criteria** (what must be TRUE):
  1. Consumers specify naming behavior via a single `NamingStrategy` enum value (`Flat`, `FlatWithForce`, `Keep`) instead of the two-field `OutputLayout`+`forceConflictHandling` combo
  2. All 3 naming strategies produce zip entry names byte-identical to the previous two-field behavior across all consumer modes (pipeline, video, picture)
  3. Pipeline consumer translates `AppConfig` naming settings to `NamingStrategy` at the call site without pack module internals changing
     4. The 945+ assertion test suite passes with zero behavioral regression
**Plans**: 2 plans

Plans:
- [x] 15-01-PLAN.md — NamingStrategy enum + NamingConfig extension + Media mode dispatch (pack.h, pack.cpp, unit tests)
- [x] 15-02-PLAN.md — Directory mode dispatch + Pipeline translation + full test verification (packer.h/.cpp, pipeline.cpp)

### Phase 16: Grouping Strategy + Summary Config on PackRequest
**Goal**: `PackRequest` fully captures grouping and summary/cover-image behavior declaratively, so no consumer needs to bypass it for complex partitioning scenarios.

**Depends on**: Phase 15
**Requirements**: SINK-02
**Success Criteria** (what must be TRUE):
  1. Consumers declare grouping behavior via `GroupingStrategy` enum (`PerSourceDir`, `PerSourceDirKeepTogether`) directly on `PackRequest`
  2. Summary entries (cover images) are injected via `SummaryConfig` on `PackRequest` with configurable prefixes, replacing hardcoded `"0000__"` / `"1000__"` string conventions
  3. Summary entries always appear first in every archive group, enforced structurally by an `isSummary` flag rather than fragile string prefix ordering
  4. Picture's two-layer logical partitioning (buckets → parts) produces identical groupings behind the `PerSourceDirKeepTogether` strategy
  5. All existing tests pass — summary injection and grouping dispatch verified via the existing integration test suite
**Plans**: 2 plans

Plans:
- [x] 16-01-PLAN.md — GroupingStrategy enum + SummaryConfig struct + isSummary flag; buildMediaPackPlan strategy dispatch + summary injection + ordering
- [x] 16-02-PLAN.md — Picture consumer migration: remove "0000__"/"1000__" prefix conventions, use isSummary structural flag

### Phase 17: Picture Process Leak Elimination
**Goal**: `picture_process.cpp` uses only the public `pack::execute(PackRequest)` API — zero internal pack type dependencies remain. All consumers are now unified on the single public entry point.

**Depends on**: Phase 15, Phase 16
**Requirements**: SINK-03
**Success Criteria** (what must be TRUE):
  1. `picture_process.cpp` no longer includes `packer_types.h`, `packer.h`, or `pack_internal.h` — all 5 internal pack includes removed
  2. Both compress and non-compress picture paths construct a single `PackRequest` and call `pack::execute()` exclusively
  3. Golden zip entry name tests pass with byte-identical output, confirming no behavioral drift in naming or grouping
  4. All 945+ assertions pass with zero behavioral change — compress, non-compress, and resumable picture workflows produce identical archives
**Plans**: 1 plan

Plans:
- [x] 17-01-PLAN.md — Remove 3 internal pack includes, delete dead code, wire PackRequest API in 3 call sites

### Phase 18: PackPlan Pure Internalization
**Goal**: `PackPlan` is provably invisible to all consumers outside `src/pack/` — a compile-time enforced encapsulation boundary. No consumer can construct or reference `PackPlan` directly.

**Depends on**: Phase 17
**Requirements**: SINK-04
**Success Criteria** (what must be TRUE):
  1. `#include "pack/pack.h"` does not expose `pack::PackPlan` — a compile test confirms it is unreachable from the public header
  2. `static_assert(std::is_aggregate_v<PackPlan>)` is removed (no longer needed once PackPlan is internal-only)
  3. All existing tests pass with zero behavioral change after PackPlan is moved to internal header
**Plans**: 1 plan

Plans:
- [x] 18-01-PLAN.md — Move PackPlan to internal header, compile-time boundary enforcement

### Phase 19: CLI11 Migration (No Color Change)
**Goal**: CLI11 replaces boost::program_options — project compiles, all 26 options parse correctly, all 3033 tests pass, zero user-visible behavioral change

**Depends on**: Phase 18 (v1.5 baseline)
**Requirements**: CLI11-01, CLI11-02, CLI11-03, CLI11-04, CLI11-05
**Success Criteria** (what must be TRUE):
  1. User invokes `encro` with any valid combination of the 26 options and gets identical parsing behavior (same defaults, same validation, same error messages for invalid input) as pre-migration
  2. User runs `encro --help` and sees the same help text layout — all 4 option groups, 26 option names/descriptions, adaptive column width — matching the pre-migration boost::po output
  3. The project builds with CLI11 as the sole CLI parsing dependency — `boost::program_options` removed from xmake.lua
  4. All 3033 existing test assertions pass with zero behavioral regression — cmd_cmd_tests and cmd_config_builder_tests rewritten for CLI11 but verify identical semantics
  5. All 58 vm.count()/vm.at() call sites across 6 consumer files (config_builder, prelude, terminal, utils, app_entry) compile and produce the same AppConfig results as pre-migration
**Plans**: 5 plans

Plans:
- [x] 19-01-PLAN.md — cmd.h CmdParseResult struct + cmd.cpp CLI11 parsing with formatter_fn and resolveHelpTextLayout()
- [x] 19-02-PLAN.md — config_builder.h/.cpp migration: buildConfig(CmdParseResult const&), 29 vm references adapted
- [x] 19-03-PLAN.md — Consumer adaptation: prelude, terminal, utils, app_entry — zero boost::po references
- [x] 19-04-PLAN.md — xmake.lua: CLI11 wired into encro/tests/e2e_tests targets
- [x] 19-05-PLAN.md — Test rewrites: cmd_cmd_tests (CLI11 integration) + cmd_config_builder_tests (results struct fixture)

### Phase 20: CLI Color Deepening
**Goal**: All CLI output (--help, --version, errors) uses semantic terminal coloring with full NO_COLOR standard compliance

**Depends on**: Phase 19
**Requirements**: COLR-01, COLR-02, COLR-03, COLR-04, COLR-05
**Success Criteria** (what must be TRUE):
  1. User runs `encro --help` and sees colored output: Usage/Heading in steel_blue, option group headers in bold steel_blue, option names in light_cyan, descriptions in plain text — all rendered via formatter_fn calling terminal::println()
  2. User runs `encro --version` and sees the version number rendered in colored output via MessageKind::Version
  3. User passes invalid options and sees error messages consistently colored in red with `[error]` badge prefix — all error paths use terminal::println(Error, ...) or terminal::eprintln()
  4. User runs `NO_COLOR=1 encro --help` and sees plain text output with zero ANSI escape codes in all sections — colorsEnabled() gates all color paths
  5. MessageKind enum has 5 new values (Usage, OptionGroup, OptionName, OptionDesc, Version) — each with completed styleFor() mapping and defaultBadgeLabel() case, added at enum end preserving backward compatibility
**Plans**: 3 plans

Plans:
- [x] 20-01-PLAN.md — MessageKind enum extension (5 new values: Usage, OptionGroup, OptionName, OptionDesc, Version) + styleFor()/defaultBadgeLabel() mappings (TDD)
- [x] 20-02-PLAN.md — Colored --help output via formatter_fn styledText() injection — Usage/steel_blue, OptionGroup/steel_blue+bold, OptionName/light_cyan, OptionDesc/plain (TDD)
- [x] 20-03-PLAN.md — --version flag + colored version output via MessageKind::Version + COLR-03 error path verification

## Progress

| Phase | Directory | Milestone | Plans Complete | Status | Completed |
|-------|-----------|-----------|----------------|--------|-----------|
| 1. Compact Progress Mode | 01-compact-progress | v1.0 | 2/2 | Complete | 2026-04-26 |
| 2. Compact Mode Gap Fixes | 02-compact-mode-gap-fixes | v1.0 | 1/1 | Complete | 2026-04-26 |
| 3. Video Subsystem Refactor | 03-video-subsystem-refactor | v1.1 | 2/2 | Complete | 2026-04-27 |
| 4. Pack Subsystem Refactor | 04-pack-subsystem-refactor | v1.1 | 2/2 | Complete | 2026-04-27 |
| 5. Picture Refactor + Final Validation | 05-picture-refactor-validation | v1.1 | 3/3 | Complete | 2026-04-27 |
| 6. Must-Fix Debt | 06-must-fix-debt | v1.2 | 3/3 | Complete | 2026-04-28 |
| 7. Structural Optimization | 07-structural-optimization | v1.2 | 1/1 | Complete | 2026-04-29 |
| 8. Type Extraction & NS Cleanup | 08-type-extraction-ns-cleanup | v1.3 | 1/1 | Complete | 2026-04-29 |
| 9. Service Class Extraction | 09-service-class-extraction | v1.3 | 4/4 | Complete | 2026-04-29 |
| 10. Dependency Injection & Testability | 10-di-and-testability | v1.3 | 5/5 | Complete | 2026-04-30 |
| 11. Consumer Migration & Cleanup | 11-consumer-migration-cleanup | v1.3 | 1/1 | Complete | 2026-04-30 |
| 12. PackRequest 声明式 API & 配置注入 | 12-packrequest-api | v1.4 | 4/4 | Complete | 2026-05-01 |
| 13. 分组统一 & 命名内化 | 13-grouping-naming | v1.4 | 4/4 | Complete | 2026-05-01 |
| 14. 移除 IPacker 抽象层 & 验证 | 14-remove-ipacker | v1.4 | 1/1 | Complete | 2026-05-01 |
| 15. Naming Strategy Enum + NamingConfig | 15-naming-strategy | v1.5 | 2/2 | Complete | 2026-05-04 |
| 16. Grouping + Summary on PackRequest | 16-grouping-summary | v1.5 | 2/2 | Complete | 2026-05-04 |
| 17. Picture Leak Elimination | 17-picture-leak | v1.5 | 1/1 | Complete | 2026-05-04 |
| 18. PackPlan Internalization | 18-packplan-internalize | v1.5 | 1/1 | Complete | 2026-05-04 |
| 19. CLI11 Migration | 19-cli11-migration | v1.6 | 5/5 | Complete | 2026-05-09 |
| 20. CLI Color Deepening | 20-cli-color-deepening | v1.6 | 3/3 | Complete | 2026-05-09 |

---

_Archive: `.planning/milestones/v1.0-ROADMAP.md`, `.planning/milestones/v1.1-ROADMAP.md`, `.planning/milestones/v1.2-ROADMAP.md`, `.planning/milestones/v1.3-ROADMAP.md`, `.planning/milestones/v1.4-ROADMAP.md`, `.planning/milestones/v1.6-ROADMAP.md`_
