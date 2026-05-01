# RETROSPECTIVE.md

## Milestone: v1.0 — Compact Progress Mode

**Shipped:** 2026-04-26
**Phases:** 2 | **Plans:** 3

### What Was Built

- Compact progress mode: single overall bar replaces per-worker slot bars in encoding, single "Packing: X/Y" bar in packing
- `--full-progress` flag restores original multi-bar behavior
- Cross-subsystem `.compact` field propagation fixed in `selectPackPlanIndexes`
- All PackPlan builders now explicitly set `.compact`

### What Worked

- Phase plans were detailed enough for autonomous execution — both phases executed without manual intervention
- Test-driven validation caught the `.compact` propagation issue in audit
- Gap closure phase (Phase 2) was quick: single plan, 2 tasks, all 3 gaps closed

### What Was Inefficient

- No VERIFICATION.md created during execute-phase — had to rely on audit for formal verification
- No REQUIREMENTS.md created upfront — requirements were implied from phase goals rather than formally tracked
- Integration gap (`selectPackPlanIndexes` dropping `.compact`) should have been caught by cross-phase tests in Phase 1

### Patterns Established

- `compact = !ctx.config.fullProgress` — consistent pattern across encoding and packing subsystems
- Explicit `.compact` in all PackPlan designated initializers — no implicit struct-default reliance
- `barIndexOpt()` safe accessor pattern for optional bar indexes in compact mode

### Key Lessons

- Always explicitly set struct fields in designated initializers — implicit defaults are fragile across refactors
- Audit after phase completion catches integration gaps that single-phase tests miss
- Gap closure phases should be small (1-2 tasks) for quick turnaround

### Cost Observations

- All work completed in a single session (2026-04-26)
- Model: deepseek-v4-pro
- 2 phases, 3 plans, 5 tasks executed
- Audit → gap closure → re-audit cycle was efficient (~2 turns)

---

## Milestone: v1.1 — Lambda Readability Refactor

**Shipped:** 2026-04-27
**Phases:** 3 | **Plans:** 7 | **Tasks:** 8

### What Was Built

- Eliminated deep lambda nesting (3+ levels) across entire codebase — max depth now ≤ 2
- 10 lambda functions extracted to named free functions in anonymous namespaces across 4 `.cpp` files
- Factory function pattern for lambda-wrapping-lambda in `selectPackPlanIndexes`
- 1-line jthread delegation pattern for monitor/spinner loops
- 0 `[&]` captures remain in `picture_process.cpp`

### What Worked

- TDD RED gate cycle for higher-risk extractions (6 RED→GREEN cycles) prevented regressions
- D-02 (individual typed parameters instead of context structs) kept function signatures clean and explicit
- Phase 3 highest-risk first, then lower-risk phases 4-5 — de-risked early
- Full test suite (910 assertions) as safety net caught zero issues — the refactoring was behavior-preserving

### What Was Inefficient

- `withActionJobState`/`withJobState` template helpers identified but deferred — shared across codebase, not scoped to v1.1
- `video_batch_execution.cpp` still 765 lines post-extraction — structural split deferred to v1.2
- 3 v1.0 known gaps (implicit `.compact`, duplicate test, missing VERIFICATION.md) carried forward

### Patterns Established

- Free functions in anonymous namespaces for extracted lambdas (internal linkage, 0 header changes)
- D-03 boundary: 2-level nesting acceptable, only 3+ targeted — prevents over-extraction
- Factory function + designated initializer pattern for lambda-wrapping-lambda
- 1-line jthread delegation: `std::jthread([&ctx] { ... })` with explicit reference capture

### Key Lessons

1. Extract highest-risk code first (Phase 3: deepest nesting) — early feedback loop
2. TDD RED gate is worth the cycle cost for complex extractions; skip it for simple 1-line renames
3. Individual typed parameters beat context structs for extracted functions — self-documenting, no indirection

### Cost Observations

- Model: deepseek-v4-pro
- 3 phases, 7 plans, 8 tasks executed
- 10 functions extracted, 0 header changes
- All 910 assertions pass with zero failures

---

## Milestone: v1.2 — Tech Debt & Code Quality

**Shipped:** 2026-04-29
**Phases:** 2 | **Plans:** 4 | **Tasks:** 6

### What Was Built

- DEBT-01: Fixed implicit `.compact` default — all 4 PackPlan sites now explicit, with `static_assert(std::is_aggregate_v<pack::PackPlan>)` guard
- DEBT-02: Removed duplicate `CHECK(result.compact == true)` at `pack_service_tests.cpp:161`
- PROC-01: Backfilled structured VERIFICATION.md for Phase 01 and Phase 02
- STRUCT-02: Split `video_batch_execution.cpp` (804→403 lines) into 2 compilation units — `video_encoding_state.cpp` (NEW, 191 lines) + `video_batch_execution.cpp` (MODIFIED)
- STRUCT-01: Cancelled after research confirmed templates already correctly placed in `video_workflow_utils.h`

### What Worked

- Research phase (FEATURES.md, ARCHITECTURE.md) paid off — STRUCT-01 cancellation saved unnecessary work
- All 3 items in Phase 6 were parallelizable (independent files) — enabled fast execution
- Forensics audit caught 3 anomalies (missing VERIFICATION.md, misnamed CONTEXT.md, stray DISCUSSION-LOG.md) before milestone close
- `videobatch::detail` namespace in header was a narrow but acceptable D-01 exception for cross-TU struct visibility

### What Was Inefficient

- `gsd-sdk` bug dropped Phase 6-7 source changes from commit — required manual recommit
- v1.2-MILESTONE-AUDIT.md not created — deferred for speed
- 6 quick tasks completed without formal status files — acknowledged as deferred items

### Patterns Established

- `static_assert(std::is_aggregate_v<PackPlan>)` guard catches accidental struct layout breakage
- Split compilation unit pattern: state/monitor vs. task execution boundary
- `videobatch::detail` namespace for shared struct definitions enabling cross-TU value semantics
- Intentional duplication of TU-local helpers (`noteStopRequest`, `truncateForProgressLabel`) as standard C++ pattern

### Key Lessons

1. Research before structural changes prevents wasted work — STRUCT-01 was right to cancel
2. Forensic audit before milestone close catches artifact gaps early
3. Explicit struct field initialization should be verified at compile time (`static_assert`), not runtime

### Cost Observations

- Model: deepseek-v4-pro
- 2 phases, 4 plans, 6 tasks executed (1 plan cancelled)
- 1 bug from tooling (`gsd-sdk` commit dropped changes)
- 6 quick tasks not formally status-tracked

---

## Milestone: v1.3 — Pack Subsystem OO Refactor

**Shipped:** 2026-04-30
**Phases:** 4 | **Plans:** 11 | **Tasks:** 11

### What Was Built

- `pack_types.h` + `packer_types.h`: shared value types extracted, circular `packer.h` ↔ `pack_service.h` dependency broken, global-scope structs moved to `pack::detail::`
- `pack::Packer final` class: 14 free functions → public/private methods, zip I/O encapsulated, ZipWriter RAII
- `pack::PackService final` class: orchestration logic encapsulated, `std::unique_ptr<IPacker>` constructor injection
- `PackProgressCallbacks` sub-struct: 5 callback `std::function` fields extracted from PackPlan
- `IPacker` abstract interface (3 virtual methods, archive granularity) + `MockPacker` capture-recording test double
- `pack_facade.h` (248 lines, 21 deprecated wrappers) created as temporary bridge, then deleted in Phase 11
- 7 consumer files migrated to direct OO API, 36 new MockPacker assertions (945 total, 225 test cases, 0 failures)

### What Worked

- Incremental delivery (4 phases, 11 plans): each plan was small enough to execute autonomously with clear verification gates
- Phase 8 (pure header refactoring) was the right first step — broke circular dependency before adding classes
- Temporary facade (`pack_facade.h`) as backward-compat bridge preserved all consumers unchanged through Phases 9-10
- `static_assert(is_aggregate_v<PackPlan>)` guard caught zero regressions — aggregate contract never violated
- TDD pattern from v1.1 carried forward: MockPacker tests written before final verification
- All classes marked `final`, zero virtual dispatch on hot paths — no performance regression

### What Was Inefficient

- Phase 9 Plan 09-02 left `packAllFilesInDirectory` as global free function temporarily — migrated to PackService in Plan 09-03; could have been done in single step
- MockPacker tests (Plan 10-04) went GREEN immediately — TDD RED gate was a no-op since MockPacker and DI were already implemented in Plans 10-01/10-03
- E2E CLI verification (Phase 11 Task 11) deferred due to test media dependency — should have been factored into pre-milestone test environment setup

### Patterns Established

- `pack_types.h` / `packer_types.h` split: shared DTO types separate from internal implementation types
- `pack::detail::` namespace for internal aliases and types (follows `videobatch::detail` precedent from v1.2)
- Minimalist abstract interface: IPacker exposes only 3 methods PackService actually calls — no over-engineering
- Capture-recording mock: simple vectors for call recording, no mock framework needed
- Per-call-site stack instances for PackService/Packer: no lifecycle management, no shared state
- `[[deprecated]]` facade as temporary bridge: removed cleanly when migration complete

### Key Lessons

1. Break circular dependencies first — Phase 8 was the critical enabler for all subsequent work
2. Temporary facade bridges reduce risk during multi-phase migrations — consumers migrate at their own pace
3. Interface granularity should match actual call sites — IPacker has exactly what PackService needs, nothing more
4. Mock without frameworks: manual capture-recording is simpler and more debuggable than mock libraries
5. E2E verification should be part of pre-milestone test environment setup, not a deferred task

### Cost Observations

- Model: deepseek-v4-pro
- 4 phases, 11 plans, 11 tasks executed
- 7 consumer files migrated, 1 facade file deleted, 2 new headers created
- 50 files changed, ~1094 source LOC added (net after deletions)
- 36 new assertions added, 0 regressions

---

## Milestone: v1.4 — Pack 接口简化 & 抽象层清理

**Shipped:** 2026-05-01
**Phases:** 3 | **Plans:** 9 | **Tasks:** 16

### What Was Built

- PackRequest declarative API: PackMode enum, NamingConfig, NamingMode, execute() declaration in single public header pack.h
- pack::execute() single entry point — 3 consumers (pipeline/video/picture) migrated, archive_plan.cpp deleted
- PackPlan fully internalized — no external consumer includes pack detail types
- Grouping unified: groupPackEntriesWithSubparts eliminates single/double-layer fork for picture vs video
- Naming internalized: NamingConfig + entryNameForFile callback replaces per-call-site name construction
- Configuration centralized: compact from AppConfig.fullProgress, all CLI params via PackRequest fields
- PackService static methods demoted to pack::internal namespace
- IPacker abstract base deleted, Packer no longer inherits anything
- PackService holds Packer by value — no unique_ptr indirection, no virtual dispatch
- MockPacker deleted — mock tests rewritten as real Packer + TempDir integration tests (126 assertions)

### What Worked

- Phase 12-14 sequential progression was right: API design → internal refactoring → abstraction removal
- pack.h as single public header kept the API surface minimal and discoverable
- NamingConfig struct with designated-initializer defaults made migration ergonomic: consumers override only what they need
- Demoting static methods to pack::internal was surgical — no behavioral change, just namespace move
- Deleting IPacker was simpler than expected: only PackService constructor and MockPacker needed changes
- Mock → integration test rewrite proved that real Packer + TempDir gives better coverage than mocks for zip I/O

### What Was Inefficient

- Phase 13 Plan 01 used map-based callback lookup (packCallbacksByApp) before realizing simpler callback-per-entry-site approach in Plan 04 — could have been first-pass design
- Quick task formal status files remain missing — 3rd milestone carrying this gap forward
- E2E CLI verification gap from v1.3 persists — test media environment never provisioned

### Patterns Established

- pack::execute() free function as single public entry point — facade-like but simpler than class wrapper
- NamingConfig with designated-initializer defaults — ergonomic partial override for configuration
- PackService value semantics: direct member, no heap allocation, no virtual dispatch
- pack::internal namespace for demoted helpers — clean public/private boundary without C++20 modules
- Integration tests over mocks for I/O-bound code: TempDir + real Packer gives better real-world coverage

### Key Lessons

1. Let the API design dictate the abstraction, not the other way around — IPacker was a v1.3 over-speculation that v1.4 removed cleanly
2. Single public header constraint forces discipline: only what consumers actually need survives
3. Value semantics beat unique_ptr when ownership is simple and deterministic — cleaner lifecycle, zero allocation
4. Named callbacks beat map-lookup callbacks for 2-3 different paths — less indirection, easier to trace
5. Integration tests with real I/O are often simpler and more valuable than mock-based tests for file system code

### Cost Observations

- Model: deepseek-v4-pro
- 3 phases, 9 plans, 16 tasks executed
- 2 files deleted (ipacker.h, packer_mock.h), 1 file deleted (archive_plan.cpp)
- 8 quick tasks completed (6 missing formal status, 2 pending todos resolved by implementation)
- 157 assertions pass (packer + pack-service integration), 945 total E2E assertions preserved

---

## Cross-Milestone Trends

### Process Evolution

| Milestone | Phases | Plans | Tasks | Key Change |
|-----------|--------|-------|-------|------------|
| v1.0 | 2 | 3 | 5 | Core feature: compact progress mode |
| v1.1 | 3 | 7 | 8 | Pure refactoring: TDD RED gate cycle introduced |
| v1.2 | 2 | 4 | 6 | Debt resolution: research-first, forensics audit |
| v1.3 | 4 | 11 | 11 | OO refactor: incremental delivery, facade bridge, DI + mocks |
| v1.4 | 3 | 9 | 16 | Interface simplification: single-entry API, abstraction removal |

### Cumulative Quality

| Milestone | Test Cases | Assertions | Files Modified | Header Changes |
|-----------|-----------|------------|----------------|----------------|
| v1.0 | 203 | 876 | 11 | Several |
| v1.1 | 215 | 910 | 4 `.cpp` files | 0 |
| v1.2 | 215 | 909 | 8 source files | 1 (narrow D-01 exception) |
| v1.3 | 225 | 945 | 19 source files + 2 new headers | 5 new/rewritten headers |
| v1.4 | 222 | 945 | 12 source files | 2 deleted, 1 simplified |

### Top Lessons (Verified Across Milestones)

1. Explicit initialization beats implicit defaults — validated across v1.0 (PackPlan `.compact`) and v1.2 (DEBT-01 fix)
2. Audit/fix/verify cycle catches integration gaps that single-phase testing misses — validated v1.0→v1.2
3. Research before structural changes prevents wasted work — validated v1.2 (STRUCT-01 cancellation)
4. TDD RED gate worth it for complex refactors, skip for trivial changes — validated v1.1→v1.2
5. Value semantics > smart pointer indirection when ownership is simple — validated v1.4 (IPacker removal)
6. Named callbacks > map-lookup for small dispatch sets — validated v1.4 (NamingConfig callback design)
7. Integration tests > mocks for I/O-bound code — validated v1.4 (MockPacker → Packer + TempDir tests)
