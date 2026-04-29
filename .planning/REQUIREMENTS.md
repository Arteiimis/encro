# Requirements: encro v1.3 Pack Subsystem OO Refactor

**Defined:** 2026-04-29
**Core Value:** Progress visibility: users always see what's happening with minimal terminal noise

## v1.3 Requirements

Requirements for the pack subsystem OO refactoring milestone.

### 1. Type Extraction & Namespace Cleanup

- [ ] **TYPE-01**: Shared value types (PackFileEntry, FileOrdinalRange, PackRunResult) extracted to independent `src/pack/pack_types.h`
- [ ] **TYPE-02**: Global-scope objects (PackGroupInput, PackGroupPartition, PackEntryInput, PackEntryPartition) moved into `pack::detail::` namespace in `src/pack/packer_types.h`
- [ ] **TYPE-03**: `packer.h` no longer includes `pack_service.h` — circular dependency resolved
- [ ] **TYPE-04**: All existing code compiles, 909 assertions pass, zero behavioral change

### 2. Service Class Extraction

- [ ] **SVC-01**: Packer class encapsulates zip I/O (libzippp) and file grouping algorithms
- [ ] **SVC-02**: PackService class encapsulates orchestration logic (packGroups, runPackPlan, selectPackPlanIndexes)
- [ ] **SVC-03**: Anonymous-namespace free functions consolidated into corresponding class private methods
- [ ] **SVC-04**: CompactProgressState formalized as private nested helper inside PackService
- [ ] **SVC-05**: Facade layer (`pack_facade.h`) provides `[[deprecated]]` free-function wrappers — video/picture/core consumers unchanged
- [ ] **SVC-06**: Progress callbacks extracted from PackPlan construction sites into PackService constructor/method parameters
- [ ] **SVC-07**: PackPlan remains aggregate struct — `static_assert(is_aggregate_v)` preserved, designated initializers preserved
- [ ] **SVC-08**: All pack classes marked `final`, method bodies in `.cpp` files, zero virtual functions in hot path

### 3. Dependency Injection & Testability

- [ ] **DI-01**: IPacker pure abstract interface (single interface, no hierarchy) — zip I/O contract
- [ ] **DI-02**: Packer implements IPacker (production implementation using libzippp)
- [ ] **DI-03**: MockPacker implements IPacker (test double capturing method calls)
- [ ] **DI-04**: PackService constructor-injected with `unique_ptr<IPacker>` (no DI framework)
- [ ] **DI-05**: ZipWriter RAII wrapper encapsulates `libzippp::ZipArchive`
- [ ] **DI-06**: IPacker granularity per-archive (`packFilesToZip`) — not per-file (avoids hot-path virtual dispatch)

### 4. Consumer Migration & Cleanup

- [ ] **MIG-01**: video_process.cpp migrated to use PackService + Packer directly
- [ ] **MIG-02**: picture_process.cpp migrated (2 PackPlan construction sites)
- [ ] **MIG-03**: archive_plan.cpp migrated to use PackService::selectPackPlanIndexes
- [ ] **MIG-04**: app/pipeline.cpp migrated
- [ ] **MIG-05**: pack_facade.h and all `[[deprecated]]` wrappers removed
- [ ] **MIG-06**: Final verification: 909 assertions pass, consumer file diffs contain only API migrations, no accidental changes

## Future Requirements

Deferred to later milestones.

- **CompactProgressState** as public class (currently private inside PackService) — promote if needed by other subsystems
- Template-based pack format abstraction (tar/7z) — only if needed
- PIMPL for ABI stability — not needed for internal CLI tool
- InMemoryZipWriter for ultra-fast tests — complement, not replacement for real-zip integration tests

## Out of Scope

Explicitly excluded from v1.3. Documented to prevent scope creep.

| Feature | Reason |
|---------|--------|
| PackPlan → class with private data | 16 designated-initializer sites + `static_assert` guard make this a 100% build break. Encapsulate logic in service classes instead |
| Virtual interface hierarchy (>1 interface) | Anti-pattern per C++ Core Guidelines C.2. Only IPacker for zip I/O mock boundary |
| Deep inheritance (>2 levels) | Zero inheritance today. Use composition. All classes `final` |
| DI framework (Boost.DI, etc.) | Constructor injection sufficient. No framework overhead |
| C++20 modules | clang-cl module support not production-ready for MSVC-ABI targets |
| Getter/setter for every data field | Anti-pattern per C++ Core Guidelines. Data-transfer types remain structs with public members |
| Header method definitions (inline) | Causes header bloat and recompilation cascades. All method bodies stay in `.cpp` |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| TYPE-01 | Phase 1 | Pending |
| TYPE-02 | Phase 1 | Pending |
| TYPE-03 | Phase 1 | Pending |
| TYPE-04 | Phase 1 | Pending |
| SVC-01 | Phase 2 | Pending |
| SVC-02 | Phase 2 | Pending |
| SVC-03 | Phase 2 | Pending |
| SVC-04 | Phase 2 | Pending |
| SVC-05 | Phase 2 | Pending |
| SVC-06 | Phase 2 | Pending |
| SVC-07 | Phase 2 | Pending |
| SVC-08 | Phase 2 | Pending |
| DI-01 | Phase 3 | Pending |
| DI-02 | Phase 3 | Pending |
| DI-03 | Phase 3 | Pending |
| DI-04 | Phase 3 | Pending |
| DI-05 | Phase 3 | Pending |
| DI-06 | Phase 3 | Pending |
| MIG-01 | Phase 4 | Pending |
| MIG-02 | Phase 4 | Pending |
| MIG-03 | Phase 4 | Pending |
| MIG-04 | Phase 4 | Pending |
| MIG-05 | Phase 4 | Pending |
| MIG-06 | Phase 4 | Pending |

**Coverage:**
- v1.3 requirements: 24 total
- Mapped to phases: 24
- Unmapped: 0 ✓

---

*Requirements defined: 2026-04-29*
*Last updated: 2026-04-29 after initial definition*
