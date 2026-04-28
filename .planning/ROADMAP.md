# encro — Roadmap

## Milestones

- ✅ **v1.0 Compact Progress Mode** — Phases 1-2 (shipped 2026-04-26)
- ✅ **v1.1 Lambda Readability Refactor** — Phases 3-5 (shipped 2026-04-27)
- ✅ **v1.2 Tech Debt & Code Quality** — Phases 6-7 (shipped 2026-04-29)

## Phases

### Phase 1: Compact Progress Mode

- [x] Phase 1: Compact Progress Mode (phases/01-compact-progress, 2/2 plans) — completed 2026-04-26

### Phase 2: Compact Mode Gap Fixes

- [x] Phase 2: Compact Mode Gap Fixes (phases/02-compact-mode-gap-fixes, 1/1 plan) — completed 2026-04-26

### Phase 3: Video Subsystem Refactor

- [x] Phase 3: Video Subsystem Refactor (phases/03-video-subsystem-refactor, 2/2 plans) — completed 2026-04-27

### Phase 4: Pack Subsystem Refactor

- [x] Phase 4: Pack Subsystem Refactor (phases/04-pack-subsystem-refactor, 2/2 plans) — completed 2026-04-27

### Phase 5: Picture Refactor + Final Validation

- [x] Phase 5: Picture Refactor + Final Validation (phases/05-picture-refactor-validation, 3/3 plans) — completed 2026-04-27

### Phase 6: Must-Fix Debt
- [x] Phase 6: Must-Fix Debt — 3/3 plans (06-01: DEBT-01, 06-02: DEBT-02, 06-03: PROC-01) — completed 2026-04-28

### Phase 7: Structural Optimization
- [x] Phase 7: Structural Optimization — STRUCT-02 (split video_batch_execution.cpp) → STRUCT-01 (relocate template helpers) cancelled

## Progress

**Execution Order:**
Phases execute in numeric order: 6 → 7

| Phase                                  | Directory                      | Milestone | Plans Complete | Status   | Completed  |
| -------------------------------------- | ------------------------------ | --------- | -------------- | -------- | ---------- |
| 1. Compact Progress Mode               | 01-compact-progress            | v1.0      | 2/2            | Complete | 2026-04-26 |
| 2. Compact Mode Gap Fixes              | 02-compact-mode-gap-fixes      | v1.0      | 1/1            | Complete | 2026-04-26 |
| 3. Video Subsystem Refactor            | 03-video-subsystem-refactor    | v1.1      | 2/2            | Complete | 2026-04-27 |
| 4. Pack Subsystem Refactor             | 04-pack-subsystem-refactor     | v1.1      | 2/2            | Complete | 2026-04-27 |
| 5. Picture Refactor + Final Validation | 05-picture-refactor-validation | v1.1      | 3/3            | Complete | 2026-04-27 |
| 6. Must-Fix Debt                       | 06-must-fix-debt                | v1.2      | 3/3            | Complete | 2026-04-28 |
| 7. Structural Optimization             | 07-structural-optimization      | v1.2      | 1/1            | Complete | 2026-04-29 |

## Phase Details

### Phase 6: Must-Fix Debt
**Goal:** Fix latent correctness bugs and backfill process compliance artifacts before structural changes.

**Requirements:** DEBT-01, DEBT-02, PROC-01

**Success criteria:**
1. `picture_process.cpp` PackPlan explicitly sets `.compact = true` (no silent struct default reliance)
2. `pack_service_tests.cpp` line 161 `CHECK(result.compact == true)` removed; all remaining 909 assertions pass
3. Structured VERIFICATION.md exists for Phase 01 and Phase 02 with requirement-to-evidence mapping
4. All 909 assertions pass unchanged across 215 test cases

**Plans (3, all parallel — independent files):**
- Plan 6-1: DEBT-01 — Fix implicit `.compact` default
- Plan 6-2: DEBT-02 — Remove redundant assertion
- Plan 6-3: PROC-01 — Backfill VERIFICATION.md

### Phase 7: Structural Optimization
**Goal:** Split `video_batch_execution.cpp` (700 lines) into 2 compilation units with zero behavioral change.

**Requirements:** STRUCT-02 — STRUCT-01 (relocate template helpers) cancelled after discussion (templates already correctly placed).

**Success criteria:**
1. `video_encoding_state.cpp` (NEW) and `video_batch_execution.cpp` (MODIFIED) compile independently
2. `EncodingProgressState` + `EncodingExecutionContext` accessible via `videobatch::detail` in header (~140 line addition, narrow D-01 exception)
3. All 909 assertions pass; 4 E2E flows produce identical binary output

**Plans (1):**
- Plan 7-1: STRUCT-02 — Split video_batch_execution.cpp

---

_Archive: `.planning/milestones/v1.0-ROADMAP.md`, `.planning/milestones/v1.1-ROADMAP.md`_
