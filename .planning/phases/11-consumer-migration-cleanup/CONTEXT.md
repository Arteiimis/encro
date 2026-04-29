# Phase 11 Context: Consumer Migration & Cleanup

**Date:** 2026-04-29
**Source:** Discussion phase (gsd-discuss-phase 11)
**Status:** Ready for planning

---

## Phase Overview

Migrate all 7 consumer files from `pack_facade.h` to direct OO API usage, remove the facade layer, and perform comprehensive cleanup + E2E verification. This is the final phase of v1.3 — after this, the pack subsystem is fully OO with DI support.

---

## Phase 10 Inheritance

Phase 10 delivered:
- `IPacker` abstract interface (3 pure virtual methods)
- `Packer : public IPacker` with ZipWriter RAII
- `PackService(std::unique_ptr<IPacker>)` constructor injection
- `MockPacker` capture-recording test double
- `pack_service_mock_tests.cpp` (10 tests, 36 assertions)
- 945 assertions pass, zero regressions

---

## Decisions Resolved

### D-01: Migration Order
**Verdict:** All 7 consumer files migrated in a single commit.

Rationale: No circular dependency among consumers. All use the same facade with the same migration pattern (facade call → OO API call). Single commit avoids intermediate states where some consumers use facade and others use OO API. Phase 11 is the last phase — facade goes away entirely.

### D-02: Grouping Function Access
**Verdict:** Consumers that need Packer grouping functions directly instantiate `PackService` which owns a Packer instance internally via `unique_ptr<IPacker>`. Consumers access grouping through their local `PackService` stack instance — the grouping calls go through `packer_->groupPackFiles(...)`.

*Wait —* Re-evaluating: Consumers that need BOTH Packer grouping AND PackService orchestration (e.g., `picture_process.cpp`) naturally get both through a single `PackService` stack instance that owns a Packer. Consumers that ONLY need Packer grouping (e.g., `video_output_planning.cpp`: just `groupPackFiles`) can still use a local `PackService` or directly instantiate Packer. The decision is:

**Consumers that need grouping + orchestration:** Create `PackService svc(std::make_unique<pack::Packer>())` — get everything through one instance.

**Consumers that need ONLY Packer grouping (no orchestration):** Directly instantiate `pack::Packer` on stack for those calls. Pure computation, no dependency management needed.

Rationale: Follows Phase 9 D-01 separation — Packer handles grouping, PackService handles orchestration. Consumers take only what they need.

### D-03: Instance Lifecycle
**Verdict:** Per-call-site stack instances — each consumer function creates its own `PackService` / `Packer` instance.

```cpp
// In consumer function:
pack::PackService svc(std::make_unique<pack::Packer>());
auto plan = svc.buildDirectoryPackPlan(dir);
auto result = svc.packFilesToZip(entries, out, timeout, callbacks);
```

Rationale: Matches current facade behavior (each call creates static instances internally). No shared state between calls. No function signature changes. Simple, predictable.

### D-04: Header Cleanup
**Verdict:** Headers that only need `PackPlan` type use `#include "pack/pack_types.h"`.

- `archive_plan.h` — needs `PackPlan`, uses `pack_types.h`
- `picture_process.h` — needs `PackPlan`, uses `pack_types.h`
- All `.cpp` files — include what they use (`pack/pack_service.h` for PackService, `pack/packer.h` for Packer)

Rationale: PackPlan moved to `pack_types.h` in Phase 8. Including `pack_service.h` for just PackPlan pulls in unnecessary transitive dependencies. `pack_types.h` is the precise include.

### D-05: E2E Verification Scope
**Verdict:** Comprehensive — 8 CLI workflow paths verified.

1. `--pack` (encode videos + pack default)
2. `--type picture` (process pictures)
3. `--pack-only` (pack only, no encoding)
4. `--full-progress` (verbose per-worker bars)
5. `--resume` (resume interrupted job)
6. `--restart` (restart from scratch)
7. Conflicting filenames / overwrite prompt
8. `--flat` output directory structure

Plus: Complete include audit on all consumer files — no stray `#include "pack/pack_facade.h"` references, no unused pack includes.

---

## Requirements

| ID | Requirement | Status |
|----|-------------|--------|
| MIG-01 | All 7 consumer files use OO API directly — no facade dependency | Pending |
| MIG-02 | `pack_facade.h` and all `[[deprecated]]` wrappers removed | Pending |
| MIG-03 | 945 assertions pass, zero regressions | Pending |
| MIG-04 | Consumer diffs show only API migrations — no accidental logic changes | Pending |
| MIG-05 | All 8 E2E CLI workflows produce identical output to v1.2 baseline | Pending |
| MIG-06 | Final cleanup — include audit, dead code removal, `using` directive review | Pending |

---

## Success Criteria

1. Zero `#include "pack/pack_facade.h"` in any source file
2. `pack_facade.h` file deleted from repository
3. All 7 consumer files compile and function correctly with direct OO API
4. 945 assertions pass, 225 test cases, zero failures
5. Consumer `git diff` shows only API migration changes — no logic touched
6. 8 E2E CLI workflows produce byte-identical output (excluding timestamps)
7. No unused `#include` directives for pack headers in consumer files
8. `using namespace pack::detail;` reviewed per-file — only where needed

---

## Implementation Constraints

| Constraint | Source | Detail |
|-----------|--------|--------|
| All-at-once migration | D-01 | Single commit for all consumers |
| Per-call-site instances | D-03 | Stack-local PackService/Packer, no shared state |
| pack_types.h for headers | D-04 | Headers use minimal include |
| Full E2E coverage | D-05 | 8 CLI paths verified |
| 945 assertions preserved | Baseline | All existing tests pass unchanged |
| No logic changes | Phase goal | Pure API migration — function calls only |

---

## Consumer Migration Map

| File | Facade Calls | Migration Target | Needs Packer? | Needs PackService? |
|------|-------------|-----------------|---------------|-------------------|
| `archive_plan.h` | `#include` only | `#include "pack/pack_types.h"` | — | — |
| `archive_plan.cpp` | `selectPackPlanIndexes`, `resolveZipNameForIndex`, `resolveProgressLabelForIndex` | Static PackService methods | — | PackService (static) |
| `picture_process.h` | `#include` only | `#include "pack/pack_types.h"` | — | — |
| `picture_process.cpp` | 8 calls (buildDirectoryPackPlan, runDirectoryPackWorkflow, packAllPicsToZip, buildPicturePackPlan, groupPackEntriesWithSubparts, groupEntryLists) | PackService + Packer | Packer (grouping) | PackService (orchestration) |
| `pipeline.cpp` | `runDirectoryPackWorkflow` | PackService | — | PackService |
| `video_output_planning.cpp` | `groupPackFiles` | Packer (grouping) | Packer | — |
| `video_process.cpp` | `buildGroupOrdinalRanges`, `appendOrdinalRangeSuffix`, `runPackPlan` | PackService + Packer | Packer (build) | PackService (run) |
| `pack_service_tests.cpp` | Direct use (not facade) | Already using PackService | — | — |
| `packer_tests.cpp` | Direct use (not facade) | Already using Packer | — | — |

---

## Key Files

| File | Role | Action |
|------|------|--------|
| `src/pack/pack_facade.h` | Facade layer | DELETE entirely |
| `src/video/video_process.cpp` | Consumer | Migrate to PackService + Packer API |
| `src/video/video_output_planning.cpp` | Consumer | Migrate to Packer API |
| `src/picture/picture_process.cpp` | Consumer (heaviest) | Migrate to PackService + Packer API |
| `src/picture/picture_process.h` | Header | Replace include with `pack_types.h` |
| `src/core/archive_plan.cpp` | Consumer | Migrate to static PackService methods |
| `src/core/archive_plan.h` | Header | Replace include with `pack_types.h` |
| `src/core/pipeline.cpp` | Consumer | Migrate to PackService API |

---

## Environment

| Component | Version |
|-----------|---------|
| OS | Windows 11 |
| Compiler | clang-cl (C++26) |
| Build tool | xmake |
| Test framework | Catch2 |
| Assertions | 945 (225 test cases) |
