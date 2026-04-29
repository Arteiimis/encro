# encro — Roadmap

## Milestones

- ✅ **v1.0 Compact Progress Mode** — Phases 1-2 (shipped 2026-04-26)
- ✅ **v1.1 Lambda Readability Refactor** — Phases 3-5 (shipped 2026-04-27)
- ✅ **v1.2 Tech Debt & Code Quality** — Phases 6-7 (shipped 2026-04-29)
- 🚧 **v1.3 Pack Subsystem OO Refactor** — Phases 8-11 (in progress)

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

## Progress

| Phase                                  | Directory                      | Milestone | Plans Complete | Status   | Completed  |
| -------------------------------------- | ------------------------------ | --------- | -------------- | -------- | ---------- |
| 1. Compact Progress Mode               | 01-compact-progress            | v1.0      | 2/2            | Complete | 2026-04-26 |
| 2. Compact Mode Gap Fixes              | 02-compact-mode-gap-fixes      | v1.0      | 1/1            | Complete | 2026-04-26 |
| 3. Video Subsystem Refactor            | 03-video-subsystem-refactor    | v1.1      | 2/2            | Complete | 2026-04-27 |
| 4. Pack Subsystem Refactor             | 04-pack-subsystem-refactor     | v1.1      | 2/2            | Complete | 2026-04-27 |
| 5. Picture Refactor + Final Validation | 05-picture-refactor-validation | v1.1      | 3/3            | Complete | 2026-04-27 |
| 6. Must-Fix Debt                       | 06-must-fix-debt               | v1.2      | 3/3            | Complete | 2026-04-28 |
| 7. Structural Optimization             | 07-structural-optimization     | v1.2      | 1/1            | Complete | 2026-04-29 |
| 8. Type Extraction & Namespace Cleanup  | 08-type-extraction-ns-cleanup  | v1.3      | 0/?            | Not started | - |
| 9. Service Class Extraction            | 09-service-class-extraction    | v1.3      | 0/?            | Not started | - |
| 10. Dependency Injection & Testability  | 10-di-and-testability         | v1.3      | 0/?            | Not started | - |
| 11. Consumer Migration & Cleanup       | 11-consumer-migration-cleanup | v1.3      | 0/?            | Not started | - |

---

## 🚧 v1.3 Pack Subsystem OO Refactor (In Progress)

**Milestone Goal:** Refactor the pack subsystem and coupled core modules to idiomatic C++ encapsulated classes with injectable dependencies and mock boundaries — preserving all 909 assertions and designated-initializer contracts.

### Phase 8: Type Extraction & Namespace Cleanup
**Goal**: Shared value types extracted to independent headers, global-scope pollution moved into `pack::` namespace, circular dependency between `packer.h` and `pack_service.h` resolved — zero behavioral change.
**Depends on**: Phase 7 (v1.2 complete)
**Requirements**: TYPE-01, TYPE-02, TYPE-03, TYPE-04
**Success Criteria** (what must be TRUE):
  1. All shared value types (PackFileEntry, FileOrdinalRange, PackRunResult) are defined in `src/pack/pack_types.h` and usable without including `pack_service.h` or `packer.h`
  2. Global-scope structs (PackGroupInput, PackGroupPartition, PackEntryInput, PackEntryPartition) are accessible only through `pack::detail::` namespace in `src/pack/packer_types.h`
  3. `packer.h` compiles without `pack_service.h` — circular dependency resolved
  4. Full test suite passes — 909 assertions across 215 test cases with zero failures
  5. All existing consumer code compiles unchanged — only `#include` paths adjusted, no logic modified
**Plans**: TBD

### Phase 9: Service Class Extraction
**Goal**: Packer and PackService classes encapsulate zip I/O, grouping algorithms, and orchestration logic. Anonymous-namespace helpers consolidated as private methods. Facade layer provides backward-compatible `[[deprecated]]` wrappers. PackPlan preserved as aggregate struct.
**Depends on**: Phase 8
**Requirements**: SVC-01, SVC-02, SVC-03, SVC-04, SVC-05, SVC-06, SVC-07, SVC-08
**Success Criteria** (what must be TRUE):
  1. `pack::Packer` class encapsulates zip I/O (libzippp) and file grouping algorithms — clear public interface, implementation details hidden as private methods
  2. `pack::PackService` class encapsulates orchestration logic (packGroups, runPackPlan, selectPackPlanIndexes) — CompactProgressState formalized as private nested helper
  3. `pack_facade.h` provides `[[deprecated]]` free-function wrappers — all 6 consumer files compile and pass tests unchanged through the facade
  4. PackPlan remains aggregate struct — `static_assert(std::is_aggregate_v)` preserved, all 16 designated-initializer construction sites unchanged
  5. All pack classes marked `final`, method bodies in `.cpp` files, zero virtual functions in hot path — no header bloat, no recompilation cascades
**Plans**: TBD
**Research needed**: Service class responsibility division (which functions go to Packer vs. PackService), callback extraction API design (PackProgressCallbacks struct vs. constructor parameters)

### Phase 10: Dependency Injection & Testability
**Goal**: IPacker abstract interface enables mock-based unit testing. PackService constructor-injected with `unique_ptr<IPacker>`. New unit tests verify orchestration without real zip I/O. Zero virtual dispatch on hot per-file paths.
**Depends on**: Phase 9
**Requirements**: DI-01, DI-02, DI-03, DI-04, DI-05, DI-06
**Success Criteria** (what must be TRUE):
  1. `IPacker` abstract interface defines zip I/O contract at archive granularity (`packFilesToZip`) — single interface, no hierarchy, no virtual calls on hot per-file paths
  2. `Packer` (production, libzippp) and `MockPacker` (test double, captures method calls) both implement `IPacker`
  3. `PackService` accepts `std::unique_ptr<IPacker>` via constructor injection — no DI framework, no singletons, no service locator
  4. `ZipWriter` RAII wrapper encapsulates `libzippp::ZipArchive` lifecycle — open/close managed automatically
  5. PackService unit tests verify orchestration logic using injected `MockPacker` — test execution does not create real zip files
**Plans**: TBD
**Research needed**: IPacker method granularity benchmark validation, MockPacker capture design (sufficient detail without becoming a test framework)

### Phase 11: Consumer Migration & Cleanup
**Goal**: All consumer subsystems (video, picture, archive_plan, app/pipeline) migrated to use PackService + Packer OO API directly. Facade layer and `[[deprecated]]` wrappers removed. Zero behavioral change — all 909 assertions pass, E2E output identical to v1.2 baseline.
**Depends on**: Phase 10
**Requirements**: MIG-01, MIG-02, MIG-03, MIG-04, MIG-05, MIG-06
**Success Criteria** (what must be TRUE):
  1. All consumer files (`video_process.cpp`, `picture_process.cpp`, `archive_plan.cpp`, `app/pipeline.cpp`) use `PackService` + `Packer` OO API directly — no facade dependency
  2. `pack_facade.h` and all `[[deprecated]]` wrappers are removed from the codebase
  3. Full test suite passes — 909 assertions across 215 test cases with zero failures
  4. Consumer file diffs (`git diff src/video/ src/picture/ src/core/ src/app/`) show only OO API migrations — no accidental logic changes, formatting drift, or behavioral differences
  5. All E2E CLI workflows (encoding+pack, pack-only, picture mode, `--full-progress`) produce identical output to pre-refactor v1.2 baseline
**Plans**: TBD

---

_Archive: `.planning/milestones/v1.0-ROADMAP.md`, `.planning/milestones/v1.1-ROADMAP.md`, `.planning/milestones/v1.2-ROADMAP.md`_
