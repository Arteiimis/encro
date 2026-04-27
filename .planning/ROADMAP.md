# encro — Roadmap

## Milestones

- ✅ **v1.0 Compact Progress Mode** — Phases 1-2 (shipped 2026-04-26)
- 🚧 **v1.1 Lambda Readability Refactor** — Phases 3-5 (planning)

## Phases

<details>
<summary>✅ v1.0 Compact Progress Mode (Phases 1-2) — SHIPPED 2026-04-26</summary>

- [x] Phase 1: Compact Progress Mode (2/2 plans) — completed 2026-04-26
- [x] Phase 2: Compact Mode Gap Fixes (1/1 plan) — completed 2026-04-26

</details>

### 🚧 v1.1 Lambda Readability Refactor (Planning)

**Milestone Goal:** Eliminate deep lambda nesting (3+ levels) and lengthy inline lambdas across the full codebase without changing any program behavior.

- [x] **Phase 3: Video Subsystem Refactor** (2/2 plans) — Extract deeply nested lambdas in video_batch_execution.cpp to named functions
- [x] **Phase 4: Pack Subsystem Refactor** (2/2 plans) — Extract lambda-wrapping-lambda and inline multiline lambdas in pack subsystem
- [ ] **Phase 5: Picture Refactor + Final Validation** — Extract named lambda variables in picture_process.cpp, validate full test suite

## Phase Details

### Phase 3: Video Subsystem Refactor
**Goal**: Eliminate deeply nested lambdas (3+ levels) in video_batch_execution.cpp, replacing them with named functions/methods
**Depends on**: Phase 2 (v1.0 shipped)
**Requirements**: REF-01
**Success Criteria** (what must be TRUE):
  1. All lambdas nested 3+ levels deep in video_batch_execution.cpp are extracted to named functions or private methods
  2. Maximum lambda nesting depth in video_batch_execution.cpp is ≤ 2 levels
  3. Video encoding pipeline compiles without errors and produces identical output
  4. All existing test cases related to video execution pass without assertion changes
**Plans**: 2 plans

Plans:
- [x] 03-01-PLAN.md — Extract inner callback and status helpers (reportEncodingStatus, markRunningNoProgress, finalizeEncodeResult)
- [x] 03-02-PLAN.md — Extract monitor body + full test suite validation

### Phase 4: Pack Subsystem Refactor
**Goal**: Eliminate lambda-wrapping-lambda and inline multiline lambdas in pack_service.cpp and packer.cpp
**Depends on**: Phase 3
**Requirements**: REF-02, REF-03
**Success Criteria** (what must be TRUE):
  1. selectPackPlanIndexes in pack_service.cpp delegates to named helper functions instead of wrapping one lambda within another
  2. packSourceEntries and spinnerThread in packer.cpp use named private methods instead of inline multiline lambdas
  3. Pack subsystem compiles without errors and produces identical packing behavior
  4. All existing test cases related to pack service and packer pass without assertion changes
**Plans**: 2 plans

Plans:
- [x] 04-01-PLAN.md — Extract lambda-wrapping-lambda in selectPackPlanIndexes (pack_service.cpp): makeSubsetZipNameResolver, makeSubsetProgressLabelResolver
- [x] 04-02-PLAN.md — Extract inline multiline lambdas in packer.cpp: packSourceEntryChunks, runFinalizingSpinner

### Phase 5: Picture Refactor + Final Validation
**Goal**: Extract named lambda variables in picture_process.cpp to static functions, with comprehensive test suite validation
**Depends on**: Phase 4
**Requirements**: REF-04, REF-05, REF-06
**Success Criteria** (what must be TRUE):
  1. addCompressTask and toJpgEntryName use static or named functions instead of named lambda variables
  2. All 876 assertions across all 203 test cases pass without any modification
  3. No behavioral changes detected — identical output for all CLI workflows (encoding+pack, pack-only, picture mode)
  4. Codebase audit confirms all targeted lambda violations are resolved (no gaps beyond documented out-of-scope exclusions)
**Plans**: TBD

## Progress

**Execution Order:**
Phases execute in numeric order: 1 → 2 → 3 → 4 → 5

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 1. Compact Progress Mode | v1.0 | 2/2 | Complete | 2026-04-26 |
| 2. Compact Mode Gap Fixes | v1.0 | 1/1 | Complete | 2026-04-26 |
| 3. Video Subsystem Refactor | v1.1 | 2/2 | Complete | 2026-04-27 |
| 4. Pack Subsystem Refactor | v1.1 | 2/2 | Complete | 2026-04-27 |
| 5. Picture Refactor + Final Validation | v1.1 | 0/TBD | Not started | - |
