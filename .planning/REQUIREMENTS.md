# Milestone v1.2 Requirements — Tech Debt & Code Quality

**Milestone:** v1.2
**Defined:** 2026-04-28
**Status:** Complete

## Category: Correctness Debt (DEBT)

- [x] **DEBT-01**: Fix implicit `.compact` default — add `.compact = true` to PackPlan designated initializer at `picture_process.cpp:474-482`. Every other PackPlan construction site explicitly sets `.compact`; this is the only site that silently relies on the struct default, creating a latent regression risk.
  - **Verifiable by:** All existing picture compress tests pass unchanged; no behavioral difference (default already provides `true`).
  - **Risk:** None — purely defensive, zero behavioral change.
  - **Files:** `src/picture/picture_process.cpp` (1 line added).

- [x] **DEBT-02**: Remove redundant assertion — delete `CHECK(result.compact == true)` at line 161 of `tests/pack_service_tests.cpp`. The compact preservation behavior is exhaustively tested by the first test case (both `true` and `false`). Keep both test cases intact; only remove the single redundant CHECK.
  - **Verifiable by:** All existing pack service tests pass with 1 fewer assertion.
  - **Risk:** None — Test B still asserts its actual concern (named helper delegation).
  - **Files:** `tests/pack_service_tests.cpp` (1 line removed).

## Category: Process Compliance (PROC)

- [x] **PROC-01**: Backfill structured VERIFICATION.md for Phase 01 (Compact Progress Mode) and Phase 02 (Compact Mode Gap Fixes). Both phases shipped without formal requirement-to-evidence mapping. Template includes: requirements coverage table, decision validation, cross-subsystem checks, coverage gaps, and environment info.
  - **Verifiable by:** VERIFICATION.md exists for both phases and maps each roadmap requirement to verifiable evidence.
  - **Risk:** None — documentation-only, no code changes.
  - **Files:** `.planning/phases/01-compact-progress-mode/VERIFICATION.md`, `.planning/phases/02-compact-mode-gap-fixes/VERIFICATION.md` (2 new files).

## Category: Structural Optimization (STRUCT)

- [x] **STRUCT-01**: Relocate `withJobState` and `withActionJobState` template helpers from `src/video/video_workflow_utils.h` (namespace `videoworkflow`) to `src/core/job_state_utils.h`. — **CANCELLED**: Templates already correctly placed; no relocation needed.
  - **Verifiable by:** Research confirmed current placement is optimal C++ pattern.
  - **Risk:** None — no change made.

- [x] **STRUCT-02**: Split `video_batch_execution.cpp` (~700 lines) into 2 compilation units: `video_encoding_state.cpp` (NEW, 191 lines) and `video_batch_execution.cpp` (MODIFIED, 403 lines). Narrow D-01 exception: relocated structs from anonymous namespace to `videobatch::detail` in existing header (+257 lines added to `video_batch_execution.h`).
  - **Verifiable by:** All existing video batch execution tests pass. Binary smoke test with all 4 E2E flows produces identical output.
  - **Risk:** Medium — anonymous namespace fragmentation, struct relocation. Requires careful extraction of leaf functions.
  - **Files:** New `src/video/video_encoding_state.cpp` + modified `src/video/video_batch_execution.cpp` + ~30 lines added to `src/video/video_batch_execution.h`.

---

## Future Requirements (Deferred)

(None — all known tech debt items are included in this milestone.)

## Out of Scope

- Header modifications beyond the narrow D-01 exception for STRUCT-02
- New C++26 features (clang-cl 22.1.4 lacks reflection/expansion statements)
- CRTP, type erasure, or `std::function` replacement for template helpers
- Splitting into more than 2 compilation units (3+ creates anonymous namespace cross-TU issues)
- Full rewrite of any subsystem
- Performance optimizations or new features — this is a cleanup-only milestone
- Adding new test cases for refactored code

## Traceability

*(Filled by roadmapper — maps each REQ-ID to the phase that delivers it.)*

| REQ-ID | Phase | Plan |
|--------|-------|------|
| DEBT-01 | 6 | 6-1 |
| DEBT-02 | 6 | 6-2 |
| PROC-01 | 6 | 6-3 |
| STRUCT-01 | 7 | 7-2 |
| STRUCT-02 | 7 | 7-1 |
