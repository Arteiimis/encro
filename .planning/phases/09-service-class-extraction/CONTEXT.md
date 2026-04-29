# Phase 9 Context: Service Class Extraction

**Date:** 2026-04-29
**Source:** Discussion phase (gsd-discuss-phase 9)
**Status:** Ready for planning

---

## Phase Overview

Consolidate 30+ free functions and anonymous-namespace helpers in `pack_service.cpp` + `packer.cpp` into two classes: `PackService` (orchestration) and `Packer` (zip I/O + grouping). PackPlan remains aggregate. Callbacks extracted into `PackProgressCallbacks`. Constructor injection introduced. All 909 assertions must pass.

---

## Phase 8 Inheritance

Phase 8 achieved the foundation:
- `pack_types.h`: PackPlan, PackFileEntry, FileOrdinalRange, PackRunResult, `kDefaultMaxArchiveGroupSize`
- `packer_types.h`: PackGroupInput, PackGroupPartition, PackEntryInput, PackEntryPartition in `pack::detail::`
- Circular dependency broken: `packer.h` → `pack_types.h` ← `pack_service.h`
- `pack::detail::` namespace for internal aliases (ZipEntryNameResolver, PackEntryProgressCallback)
- 909 assertions pass, zero behavioral change

---

## Decisions Resolved

### D-01: Function-to-Class Mapping
**Verdict:** PackService = orchestrator, Packer = zip I/O + grouping.

**PackService class** (in `pack_service.h` / `pack_service.cpp`):
- `packFilesToZip()` — main pack orchestration entry
- `packGroupsCompact()` — compact mode spinner + progress I/O
- `packGroups()` — full progress mode with per-group output
- `splitSourceDirectoryEntries()` — monolithic callback dispatch
- `packSourceEntryChunks()` — subgroup chunking
- `runFinalizingSpinner()` — zip.close() spinner
- CompactProgressState — private nested helper (stays in `.cpp`)

**Packer class** (in `packer.h` / `packer.cpp`):
- `buildDirectoryPackPlan()` → PackPlan from directory scan
- `runDirectoryPackWorkflow()` → full pack flow entry
- `groupPackFiles()` — per-file grouping
- `groupPackEntriesWithSubparts()` — entry-level grouping
- `groupEntryLists()` — list-level grouping
- `buildArchivePlan()` — from PackPlan to ordered ArchiveEntry sequence

**Rule:** If it touches PackPlan creation/validation → PackService. If it's pure file I/O or grouping → Packer.

### D-02: Callback Extraction (SVC-08)
**Verdict:** Extract to `PackProgressCallbacks` struct in Phase 9 (not deferred).

The 6 `std::function` fields on PackPlan:
```cpp
struct PackProgressCallbacks {
    std::function<void(const std::string&)> onPackFileMessage;            // per-file message
    std::function<void(std::string_view, std::string_view, int, int)> onPackGroupProgress;  // group progress
    std::function<void()> onPackComplete;                                 // completion
    std::function<void(std::string_view)> onConfirmOverwrite;             // user prompt
    std::function<void(std::string_view, int64_t, int64_t)> onProgressUpdate; // byte-level
    std::function<void(std::string_view, std::string_view)> onZipStatus;  // zip state
};
```

PackPlan gains a single field: `PackProgressCallbacks progressCallbacks{};` — the 6 individual callbacks are removed from PackPlan. This is structurally a rename: PackPlan still holds callbacks, just nested in a sub-struct. `static_assert(is_aggregate_v)` preserved — designated initializers work with sub-aggregates.

### D-03: Constructor Injection (SVC-01)
**Verdict:** PackService constructor takes Packer dependency in Phase 9.

```cpp
class PackService final {
public:
    explicit PackService(pack::Packer& packer);
    // ...
private:
    pack::Packer& packer_;  // non-owning reference
};
```

Rationale: Phase 10 introduces `IPacker` interface; changing from `Packer&` to `std::unique_ptr<IPacker>` is a one-line swap. Starting with constructor injection now means consumer migration in Phase 11 is already correct.

### D-04: Facade Design
**Verdict:** Static method wrappers in `pack_facade.h`.

```cpp
namespace pack_facade {
    [[deprecated("Use PackService::packFilesToZip")]]
    inline auto packFilesToZip(const pack::PackPlan& plan) {
        static pack::Packer defaultPacker;
        static pack::PackService defaultService(defaultPacker);
        return defaultService.packFilesToZip(plan);
    }
}
```

Rationale: Facade is temporary (removed in Phase 11). Static methods mean consumers only change `#include` paths — zero call-site changes. No lifecycle management burden.

### D-05: CompactProgressState Encapsulation
**Verdict:** Keep as private nested struct defined in `.cpp` (not exposed in header).

Rationale: CompactProgressState is an internal implementation detail of `packGroupsCompact()`. Headers stay minimal. Follows existing Phase 8 pattern of `.cpp`-local types.

---

## Requirements

| ID | Requirement | Status |
|----|-------------|--------|
| SVC-01 | PackService class — CompactProgressState as private nested helper, constructor-injected Packer | Pending |
| SVC-02 | Packer class — zip I/O and grouping algorithms encapsulated | Pending |
| SVC-03 | PackPlan remains aggregate — static_assert(is_aggregate_v) preserved, 16 designated-initializer sites unchanged | Pending |
| SVC-04 | All pack classes `final`, method bodies in `.cpp`, zero virtual in hot path | Pending |
| SVC-05 | pack_facade.h with static [[deprecated]] wrappers — all 6 consumers compile unchanged | Pending |
| SVC-06 | 30+ free functions consolidated into PackService + Packer methods | Pending |
| SVC-07 | packer.h grouping functions cleanly integrated into Packer | Pending |
| SVC-08 | 6 PackPlan callbacks extracted into PackProgressCallbacks sub-struct | Pending |

---

## Success Criteria

1. PackService class with all orchestration methods, constructor-injected Packer dependency
2. Packer class with all zip I/O and grouping methods, marked `final`
3. PackPlan aggregate with `static_assert` preserved; 6 callbacks replaced by single `PackProgressCallbacks progressCallbacks` field
4. `pack_facade.h` — all 6 consumers compile via `[[deprecated]]` static wrappers
5. Zero anonymous-namespace functions remain without justification in `pack_service.cpp` and `packer.cpp`
6. All pack class method bodies in `.cpp` files (not headers)
7. 909 assertions pass, zero behavioral change
8. Header line counts within ~120% of Phase 8 baseline

---

## Implementation Constraints

| Constraint | Source | Detail |
|-----------|--------|--------|
| PackPlan stays aggregate | v1.2 + PROJECT.md | `static_assert(is_aggregate_v)` preserved; designated initializers work with sub-aggregate callbacks |
| All pack classes `final` | PITFALLS.md | No inheritance; maximum hierarchy depth = 0 |
| Zero virtual in hot path | PITFALLS.md | `packFilesToZip` called per-file; virtual dispatch would defeat LTO |
| Method bodies in .cpp | Header bloat prevention | Headers stay lean; no transitive include costs |
| Constructor injection | D-03 | PackService(Packer&) now; migrate to IPacker in Phase 10 |
| Facade zero call-site changes | D-04 | Only #include changes at consumer sites |
| 909 assertions unchanged | Baseline | No test modifications except facade include swaps |

---

## Key Files

| File | Role | Action |
|------|------|--------|
| `src/pack/pack_service.h` | **NEW** PackService class declaration | Class with all public methods + `static_assert(is_aggregate_v<PackPlan>)` preserved |
| `src/pack/pack_service.cpp` | **MODIFIED** PackService method bodies | Free functions → class methods; CompactProgressState as private nested struct in .cpp |
| `src/pack/packer.h` | **NEW** Packer class declaration | Class `final` with all zip/grouping methods; PackProgressCallbacks struct |
| `src/pack/packer.cpp` | **MODIFIED** Packer method bodies | Free functions → class methods |
| `src/pack/pack_facade.h` | **NEW** Facade | `[[deprecated]]` static wrappers forwarding to Packer/PackService |
| `src/pack/pack_types.h` | **MODIFIED** | Remove 6 callback fields from PackPlan, add `PackProgressCallbacks progressCallbacks` field |
| 6 consumer files | **MODIFIED** | Swap includes to `pack_facade.h` |

---

## Environment

| Component | Version |
|-----------|---------|
| OS | Windows 11 |
| Compiler | clang-cl (C++26) |
| Build tool | xmake |
| Test framework | Catch2 |
| Assertions | 909 (215 test cases) |
