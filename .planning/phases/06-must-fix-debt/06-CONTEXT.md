# Phase 6: Must-Fix Debt - Context

**Gathered:** 2026-04-28
**Status:** Ready for planning

## Phase Boundary

Fix three independent correctness/process items with zero behavioral change:
- **DEBT-01**: Add explicit `.compact = true` to PackPlan at `picture_process.cpp:474-482`
- **DEBT-02**: Remove redundant `CHECK(result.compact == true)` at `pack_service_tests.cpp:161`
- **PROC-01**: Backfill two VERIFICATION.md files (Phase 01 + Phase 02)

All three items share no code paths and can execute in parallel. All 910 existing assertions must pass unchanged.

## Implementation Decisions

### DEBT-01: Explicit .compact Default
- **D-01:** Add `.compact = true` as the LAST designated initializer field (after `.removeOnFailure`), following PackPlan declaration order.
- **D-02:** Add `static_assert(std::is_aggregate_v<pack::PackPlan>)` in `pack_service.h` or `pack_service_tests.cpp` to verify PackPlan remains aggregate-initializable (defensive measure per PITFALLS-2).
- **Rationale:** Every other PackPlan construction site explicitly sets `.compact` (3 sites in `video_process.cpp:434`, `packer.cpp:820`, test files). This is the only site relying on struct default. Changing the default would silently break only this path.

### DEBT-02: Remove Redundant Assertion
- **D-03:** Remove ONLY line 161 `CHECK(result.compact == true)` from `pack_service_tests.cpp`. Keep both test cases intact.
- **Rationale:** Test A (lines 98-130) exhaustively tests compact preservation for both `true` and `false`. Test B (lines 132-162) tests v1.1 named helper delegation — the compact assertion at line 161 is vestigial.

### PROC-01: VERIFICATION.md Backfill
- **D-04:** Two separate files: `phases/01-compact-progress/VERIFICATION.md` and `phases/02-compact-mode-gap-fixes/VERIFICATION.md`.
- **D-05:** Follow FEATURES.md template: requirements coverage table, decision validation, cross-subsystem checks, coverage gaps, environment info.

### Test Gate
- **D-06:** Run full test suite (910 assertions, 215 test cases) before ANY changes to establish baseline. Run again after all changes. Zero failures expected at both points.

### the agent's Discretion
- [DEBT-01] Exact placement: append as `.compact = true,` on the line after `.removeOnFailure = true`.
- [DEBT-02] Verify that Test A actually covers compact=true and compact=false. Confirm no other assertions reference compact in Test B.
- [PROC-01] Source evidence from existing v1.0-MILESTONE-AUDIT.md + test file contents. Do not re-run tests for verification purposes.

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Project-level
- `.planning/PROJECT.md` §Requirements.Validated, §Key Decisions, §Known Issues — v1.2 scope and constraints
- `.planning/REQUIREMENTS.md` — DEBT-01, DEBT-02, PROC-01 with verifiability criteria
- `.planning/ROADMAP.md` §Phase 6 — goal, success criteria, plan list

### Research (all in `.planning/research/`)
- `.planning/research/FEATURES.md` §Item-by-Item Deep Dive — exact line numbers, before/after code, verification steps
- `.planning/research/ARCHITECTURE.md` §Q3 — PackPlan construction site inventory, `static_assert` recommendation
- `.planning/research/PITFALLS.md` §Pitfall 1 (DEBT-02 — keep both test cases), §Pitfall 2 (DEBT-01 — declaration order)
- `.planning/research/STACK.md` — zero tooling changes needed (clang-cl 22.1.4, xmake auto-discovers)

### Source code
- `src/picture/picture_process.cpp:474-482` — current PackPlan construction (missing `.compact`)
- `src/pack/pack_service.h:48` — `bool compact = true` struct default
- `tests/pack_service_tests.cpp:98-162` — two test cases with redundant line 161

### Verification template
- `.planning/research/FEATURES.md` §DEBT-03 — VERIFICATION.md template with valuable vs redundant content table

## Specific Ideas

None — execution steps are fully defined by research. All three items are 1-line changes or documentation with fixed templates.

## Deferred Ideas

None — discussion stayed within phase scope.

---

*Phase: 6-Must-Fix Debt*
*Context gathered: 2026-04-28*
