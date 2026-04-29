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

## Cross-Milestone Trends

### Process Evolution

| Milestone | Phases | Plans | Tasks | Key Change |
|-----------|--------|-------|-------|------------|
| v1.0 | 2 | 3 | 5 | Core feature: compact progress mode |
| v1.1 | 3 | 7 | 8 | Pure refactoring: TDD RED gate cycle introduced |
| v1.2 | 2 | 4 | 6 | Debt resolution: research-first, forensics audit |

### Cumulative Quality

| Milestone | Test Cases | Assertions | Files Modified | Header Changes |
|-----------|-----------|------------|----------------|----------------|
| v1.0 | 203 | 876 | 11 | Several |
| v1.1 | 215 | 910 | 4 `.cpp` files | 0 |
| v1.2 | 215 | 909 | 8 source files | 1 (narrow D-01 exception) |

### Top Lessons (Verified Across Milestones)

1. Explicit initialization beats implicit defaults — validated across v1.0 (PackPlan `.compact`) and v1.2 (DEBT-01 fix)
2. Audit/fix/verify cycle catches integration gaps that single-phase testing misses — validated v1.0→v1.2
3. Research before structural changes prevents wasted work — validated v1.2 (STRUCT-01 cancellation)
4. TDD RED gate worth it for complex refactors, skip for trivial changes — validated v1.1→v1.2
