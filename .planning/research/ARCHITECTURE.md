# Architecture Research — OO Refactoring of Pack Subsystem

**Domain:** C++26 CLI tool — OO encapsulation of pack subsystem and coupled core modules
**Researched:** 2026-04-29
**Confidence:** HIGH
**Milestone:** v1.3 Pack Subsystem OO Refactor

## System Overview — Current Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                          app/ (entry point)                          │
│   app_entry.cpp → prelude.cpp → pipeline.cpp                        │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌──────────────┐  ┌──────────────────┐  ┌───────────────────────┐  │
│  │   video/     │  │   picture/        │  │   pack/               │  │
│  │  video_      │  │  picture_         │  │  pack_service.h/.cpp  │  │
│  │  process.cpp │  │  process.h/.cpp   │  │  packer.h/.cpp        │  │
│  │  (constructs │  │  (constructs      │  │  PackPlan (struct)    │  │
│  │   PackPlan,  │  │   PackPlan,       │◄─┤  free functions       │  │
│  │   calls      │  │   calls           │  │  packer free funcs    │  │
│  │   pack::     │  │   pack::          │  │                       │  │
│  │   runPackPlan│  │   runPackPlan,    │  │                       │  │
│  │   )          │  │   packGroups)     │  │                       │  │
│  └──────┬───────┘  └────────┬─────────┘  └───────────┬───────────┘  │
│         │                   │                         │              │
│  ┌──────┴───────────────────┴─────────────────────────┴───────────┐  │
│  │                         core/                                    │  │
│  │  archive_plan.h/.cpp (uses pack::PackPlan for resumable jobs)    │  │
│  │  task_executor.h/.cpp (task::TaskSpec with std::function run)   │  │
│  │  job_state.h/.cpp (jobstate::Store — already a class)            │  │
│  │  app_context.h (AppContext, AppConfig, RuntimeContext structs)   │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                                                      │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │                        infra/                                     │  │
│  │  terminal, stop_signal, crash_runtime, toolchain, console_width  │  │
│  └──────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
```

## Target Architecture — After OO Refactoring

```
┌─────────────────────────────────────────────────────────────────────┐
│                          app/ (entry point)                          │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌──────────────┐  ┌──────────────────┐  ┌───────────────────────┐  │
│  │   video/     │  │   picture/        │  │   pack/               │  │
│  │  (uses       │  │  (uses           │  │                       │  │
│  │   pack::     │  │   pack::         │  │  pack_types.h          │  │
│  │   types +    │  │   types +        │  │   PackFileEntry (struct)│  │
│  │   PackService│  │   PackService)   │  │   FileOrdinalRange     │  │
│  │   )          │  │                  │  │   PackRunResult        │  │
│  └──────┬───────┘  └────────┬─────────┘  │                       │  │
│         │                   │            │  pack_plan.h/.cpp      │  │
│         │                   │            │   PackPlan (class)     │  │
│         │                   │            │   private data members │  │
│         │    ┌──────────────┴──────────┐ │   public methods       │  │
│         │    │   pack/ (Public API)    │ │                       │  │
│         └────┤  PackService            │ │  pack_service.h/.cpp   │  │
│              │  Packer                 │ │   PackService (class)  │  │
│              │  IPacker (abstract)     │ │   (orchestration)      │  │
│              └─────────────────────────┘ │                       │  │
│                                          │  packer.h/.cpp         │  │
│                                          │   Packer (class)       │  │
│                                          │   implements IPacker   │  │
│                                          │   zip I/O, grouping    │  │
│                                          │                       │  │
│                                          │  packer_types.h        │  │
│                                          │   PackGroupInput etc.  │  │
│                                          │   in pack::detail::    │  │
│                                          └───────────────────────┘  │
│                                                                      │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │                         core/                                     │  │
│  │  archive_plan.h/.cpp  (uses pack::PackPlan, pack::PackService)   │  │
│  │  task_executor.h/.cpp (unchanged)                                │  │
│  │  job_state.h/.cpp     (unchanged — already a class)              │  │
│  │  app_context.h        (unchanged)                                 │  │
│  └──────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
```

## Component Boundaries

### Before Refactoring (Current)

| Component | Kind | Responsibility | Coupling |
|-----------|------|----------------|----------|
| `pack::PackPlan` | aggregate struct | Holds zipping plan (groups, callbacks, config) | Directly accessed by video, picture, core modules via designated initializers |
| `pack::packGroups()` | free function | Routes to compact/full packing paths | Called by pack_service.cpp, picture_process.cpp |
| `pack::runPackPlan()` | free function | Orchestrates packing with optional job state resumability | Called by video_process.cpp, picture_process.cpp, packer.cpp |
| `pack::selectPackPlanIndexes()` | free function | Filters PackPlan by index subset for resumable jobs | Called by archive_plan.cpp (core/) |
| `pack::buildGroupOrdinalRanges()` | free function | Computes ordinal ranges for naming | Called by video_process.cpp, picture_process.cpp |
| `packFilesToZip()` (3 overloads) | free functions (global/`pack::`) | Low-level zip I/O | Called by pack_service.cpp |
| `groupFilesBySize()` | free function (global) | Groups files by size limit | Called by video_output_planning.cpp, packer.cpp |
| `groupPackFiles()` | free function (global) | Groups files preserving source dir affinity | Called by video_output_planning.cpp |
| `buildDirectoryPackPlan()` | free function (global) | Builds PackPlan for raw directory packing | Called by packer.cpp, app/pipeline.cpp |
| `jobstate::Store` | **class** | Resumable job state persistence | Already OO; used by pack, video subsystems |

### After Refactoring (Target)

| Component | Kind | Responsibility | Coupling |
|-----------|------|----------------|----------|
| `pack::PackFileEntry` | struct (unchanged) | Value type for zip entry metadata | Cross-subsystem value type — stays public |
| `pack::FileOrdinalRange` | struct (unchanged) | Ordinal range data | Utility type — stays public |
| `pack::PackRunResult` | struct (unchanged) | Result value | Return type — stays public |
| `pack::PackPlan` | **class** (NEW) | Encapsulated pack plan with private data + public query/mutation methods | Video/picture construct via builder/factory, not designated initializer |
| `pack::PackService` | **class** (NEW) | Orchestration: runs pack plans, manages compact/full dispatch, resumability | Video/picture call `service.runPackPlan()` |
| `pack::IPacker` | **abstract interface** (NEW) | Zip I/O contract for mockability | Implemented by `pack::Packer`, mocked in tests |
| `pack::Packer` | **class** (NEW) | Concrete zip I/O: creates archives, groups files, handles libzippp | Implements `IPacker`; used by `PackService` |
| `pack::detail::*` | internal types | Packer input types (PackGroupInput, etc.) | Not visible outside pack subsystem |

## Data Flow — Before and After

### Current Flow: video_process.cpp → pack
```
video_process.cpp
  → constructs pack::PackPlan via designated initializer (direct member access)
  → calls pack::runPackPlan(ctx, plan) — free function
    → calls pack::packGroups(plan) — free function
      → calls packFilesToZip(entries, zipPath, ...) — free function
        → calls libzippp::ZipArchive directly
```

### Target Flow: video_process.cpp → pack
```
video_process.cpp
  → constructs pack::PackPlan via factory (no direct member access)
  → calls packService.runPackPlan(ctx, plan) — method on injected PackService
    → PackService::packGroups(plan) — private method
      → m_packer->packFilesToZip(entries, zipPath, ...) — method on IPacker
        → libzippp::ZipArchive (hidden behind interface)
```

## Recommended Project Structure

```
src/
├── pack/
│   ├── pack_types.h              # ★ Public value types: PackFileEntry, FileOrdinalRange, PackRunResult
│   ├── pack_plan.h               # ★ PackPlan class declaration (replaces struct in pack_service.h)
│   ├── pack_plan.cpp             # PackPlan implementation
│   ├── pack_service.h            # ★ PackService class (orchestration, replaces free functions)
│   ├── pack_service.cpp          # PackService implementation
│   ├── packer_interface.h        # ★ IPacker abstract interface (for mockability)
│   ├── packer.h                  # ★ Packer class (concrete zip I/O)
│   ├── packer.cpp                # Packer implementation
│   ├── packer_types.h            # Internal types: PackGroupInput, PackEntryInput → pack::detail::
│   └── pack_facade.h             # [TEMPORARY] Deprecated free-function wrappers for backward compat
├── video/
│   ├── video_process.cpp         # MODIFIED: uses PackService, not free functions
│   ├── video_output_planning.cpp # MODIFIED: uses Packer, not free groupPackFiles
│   └── ... (other files unchanged)
├── picture/
│   ├── picture_process.cpp       # MODIFIED: uses PackService + Packer
│   ├── picture_process.h         # MODIFIED: return types still pack::PackPlan
│   └── ... (other files unchanged)
├── core/
│   ├── archive_plan.cpp          # MODIFIED: uses PackPlan methods + PackService
│   └── ... (other files unchanged)
├── app/
│   ├── pipeline.cpp              # MODIFIED: uses Packer class, not free functions
│   └── ... (other files unchanged)

tests/
├── pack/
│   ├── pack_plan_tests.cpp         # NEW: test PackPlan encapsulation
│   ├── pack_service_tests.cpp      # MODIFIED: test PackService with mock Packer
│   ├── packer_tests.cpp            # MODIFIED: test Packer class
│   └── pack_integration_tests.cpp  # NEW: integration tests
├── video/
│   └── ... (minimal changes)
├── picture/
│   └── ... (minimal changes)
```

### Structure Rationale

- **`pack_types.h`**: Extracted first to break the circular `packer.h` → `pack_service.h` dependency. Both headers now include only `pack_types.h` for shared value types (PackFileEntry).
- **`pack_plan.h/.cpp`**: PackPlan becomes a class with private members. This is the core encapsulation change. Separated from pack_service.h to clarify that PackPlan is a data model, not a service.
- **`packer_interface.h`**: IPacker abstract interface enables unit testing of PackService without real zip I/O. PackService depends on the interface, not the concrete Packer.
- **`pack_facade.h`**: Temporary backward compatibility layer. Keeps old free function signatures as `[[deprecated]]` wrappers delegating to new classes. Removed once all consumers migrate.
- **`packer_types.h`**: Isolates internal packer input types (`PackGroupInput`, `PackEntryInput`, etc.) into `pack::detail::` namespace. Prevents these implementation details from leaking through the public header.

## Architectural Patterns

### Pattern 1: Interface-Based Dependency Injection (for Packer)

**What:** PackService depends on `IPacker` abstract interface, not concrete `Packer`. Concrete `Packer` injected at construction or via setter.

**When to use:** Whenever a component (PackService) needs to call side-effect-heavy operations (zip I/O, filesystem access) that must be mockable for unit testing.

**Trade-offs:**
- **Pros:** Enables unit testing of PackService without real zip archives. Clean separation of orchestration (PackService) from I/O (Packer). Follows existing precedent (`jobstate::Store` is already a class with private members).
- **Cons:** Adds one abstract interface (+~15 lines). Virtual dispatch overhead is negligible for zip I/O (dominated by filesystem and compression).

**Example:**
```cpp
// packer_interface.h
namespace pack {

class IPacker {
public:
  virtual ~IPacker() = default;

  virtual auto packFilesToZip(
    std::vector<PackFileEntry> const& entries,
    std::filesystem::path const& zipFilePath,
    PackEntryProgressCallback onEntryPacked = {},
    std::atomic<std::size_t>* finalizingCount = nullptr
  ) -> eh::Result<void> = 0;

  // ... other zip/group operations
};

} // namespace pack

// pack_service.h
namespace pack {

class PackService {
public:
  explicit PackService(std::unique_ptr<IPacker> packer);

  auto packGroups(PackPlan const& plan) -> eh::Result<std::vector<fs::path>>;
  auto runPackPlan(appctx::AppContext& ctx, PackPlan const& plan)
    -> eh::Result<PackRunResult>;
  auto selectPackPlanIndexes(PackPlan const& plan, std::span<std::size_t const> indexes)
    -> PackPlan;

private:
  std::unique_ptr<IPacker> m_packer;
  // implementation details hidden here
};

} // namespace pack
```

### Pattern 2: PackPlan as Class with Builder (encapsulation)

**What:** PackPlan transitions from aggregate struct (all public members, designated initializer construction) to a class with private data members and a fluent builder or constructor parameter struct.

**When to use:** When a data structure has invariants (e.g., `groups` must be non-empty for certain operations, `zipNameForIndex` mutex with `compact`) and is constructed in 4+ sites with similar patterns.

**Trade-offs:**
- **Pros:** Encapsulation prevents accidental direct mutation of callbacks. Builder ensures all required fields are set. Enables validation in constructor. Eliminates `static_assert(is_aggregate_v)` guard.
- **Cons:** Breaking change from designated initializers. All 4 construction sites (video_process, picture_process × 2, packer's buildDirectoryPackPlan) must change. **Mitigated by facade pattern** — old sites continue working via deprecated constructor until migrated.

**Example:**
```cpp
// pack_plan.h
namespace pack {

class PackPlan {
public:
  // Builder struct for fluent construction (preserves readability of designated initializers)
  struct Builder {
    std::vector<std::vector<PackFileEntry>> groups;
    std::filesystem::path outputDir;
    std::function<std::string(std::size_t)> zipNameForIndex;
    std::function<std::string(std::size_t)> progressLabelForIndex;
    std::optional<std::size_t> maxParallelJobs;
    bool removeOnFailure = false;
    bool compact = true;
  };

  explicit PackPlan(Builder builder);

  // Queries
  [[nodiscard]] auto groups() const -> std::vector<std::vector<PackFileEntry>> const&;
  [[nodiscard]] auto outputDir() const -> std::filesystem::path const&;
  [[nodiscard]] auto compact() const -> bool;
  [[nodiscard]] auto maxParallelJobs() const -> std::optional<std::size_t>;

  // These are used by archive_plan.cpp for resumable jobs
  [[nodiscard]] auto zipNameForIndex(std::size_t index) const -> std::string;
  [[nodiscard]] auto progressLabelForIndex(std::size_t index) const -> std::string;

  // For selectPackPlanIndexes — creates a filtered copy
  auto withGroups(std::vector<std::vector<PackFileEntry>> newGroups) const -> PackPlan;

private:
  std::vector<std::vector<PackFileEntry>> m_groups;
  std::filesystem::path m_outputDir;
  std::function<std::string(std::size_t)> m_zipNameForIndex;
  std::function<std::string(std::size_t)> m_progressLabelForIndex;
  std::optional<std::size_t> m_maxParallelJobs;
  bool m_removeOnFailure = false;
  bool m_compact = true;
  // Callbacks — internal, managed by PackService not exposed publicly
};

} // namespace pack
```

**Design decision — callbacks stay internal:** The `onGroupStart`, `onGroupSuccess`, `onGroupFailure`, `onCompactProgress`, `onCompactStatusText` callbacks are NOT part of the public PackPlan API. They are set by `PackService::runPackPlan` (for resumable job state) or not needed at all in the public interface. This reduces PackPlan's public surface from 15 fields to 6 meaningful accessors.

### Pattern 3: Facade Pattern for Backward Compatibility

**What:** Keep old free-function signatures as `[[deprecated]]` wrappers in a separate header (`pack_facade.h`) that delegate to new class methods. All existing video/picture code continues to compile and pass tests without changes during the transition.

**When to use:** When migrating a public API consumed by multiple subsystems that you don't want to change atomically.

**Trade-offs:**
- **Pros:** Zero-risk migration. Tests pass at every step. Video/picture subsystems can be migrated on their own schedule. Commit at every step with green CI.
- **Cons:** Temporary duplication (facade functions + class methods). Must remember to remove facade after all consumers migrate.

**Example:**
```cpp
// pack_facade.h — TEMPORARY, removed after all consumers migrate
#pragma once
#include "pack/pack_service.h"
#include "pack/packer.h"

namespace pack {

// Facade: delegates to PackService singleton or injected instance
[[deprecated("Use PackService::packGroups instead")]]
inline auto packGroups(PackPlan const& plan) -> eh::Result<std::vector<fs::path>> {
  PackService service(std::make_unique<Packer>());
  return service.packGroups(plan);
}

[[deprecated("Use PackService::runPackPlan instead")]]
inline auto runPackPlan(appctx::AppContext& ctx, PackPlan const& plan)
  -> eh::Result<PackRunResult> {
  PackService service(std::make_unique<Packer>());
  return service.runPackPlan(ctx, plan);
}

// ... etc for all migrated free functions

} // namespace pack

// packer.h also exposes deprecated free function wrappers:
[[deprecated("Use Packer::packFilesToZip instead")]]
inline auto packFilesToZip(/* signature matching old overloads */) -> eh::Result<void> {
  Packer packer;
  return packer.packFilesToZip(/* forwarded args */);
}

// ... etc
```

## Integration Points — Specific File/Struct References

### What Changes vs What Stays

#### NEW Files

| File | Purpose | Dependencies |
|------|---------|-------------|
| `src/pack/pack_types.h` | Public value types (PackFileEntry, FileOrdinalRange, PackRunResult) | `<filesystem>`, `<vector>`, `<string>` |
| `src/pack/pack_plan.h` | PackPlan class declaration | `pack/pack_types.h`, `core/error_handle.h` |
| `src/pack/pack_plan.cpp` | PackPlan implementation (builder validation, utility methods) | `pack/pack_plan.h`, `<format>` |
| `src/pack/packer_interface.h` | IPacker abstract base class | `pack/pack_types.h`, `core/error_handle.h` |
| `src/pack/packer_types.h` | Internal types: `pack::detail::PackGroupInput`, `detail::PackEntryInput`, etc. | `pack/pack_types.h`, `<filesystem>`, `<optional>` |

#### MODIFIED Files (pack subsystem — core refactoring)

| File | Changes | Reason |
|------|---------|--------|
| `src/pack/pack_service.h` | Becomes PackService class declaration. Removes PackPlan struct (moves to pack_plan.h). Removes free function declarations (become class methods or deprecated facades). | Core encapsulation target |
| `src/pack/pack_service.cpp` | Implementation moves into PackService methods. `CompactProgressState` becomes private nested class or private member. Anonymous namespace functions become private methods. | All 455 lines reorganized under class |
| `src/pack/packer.h` | Becomes Packer class declaration. Implements IPacker. Removes global-scope structs (move to packer_types.h). Removes free function declarations (become class methods or deprecated facades). | Core encapsulation target |
| `src/pack/packer.cpp` | Implementation moves into Packer methods. Anonymous namespace helpers become private methods. No longer includes pack_service.h (includes only pack_types.h for PackFileEntry). | Breaks circular dependency |

#### MODIFIED Files (consumers — minimal changes)

| File | Change Type | Lines Affected | What Changes |
|------|-------------|---------------|--------------|
| `src/video/video_process.cpp` | LIGHT | ~50 lines (395-448) | PackPlan construction: designated initializer → `PackPlan::Builder{}`. `pack::runPackPlan(ctx, plan)` → `service.runPackPlan(ctx, plan)`. Add `#include "pack/pack_service.h"` and `PackService` instantiation. **Facade phase: ZERO changes.** |
| `src/video/video_output_planning.cpp` | TRIVIAL | ~5 lines | `groupPackFiles(packInputs, ...)` → `packer.groupPackFiles(...)`. **Facade phase: ZERO changes.** |
| `src/picture/picture_process.cpp` | LIGHT | ~110 lines (2 PackPlan sites + packGroups/runPackPlan calls) | Same as video — Builder + PackService. **Facade phase: ZERO changes.** |
| `src/picture/picture_process.h` | NONE initially | 0 lines | Return type remains `eh::Result<pack::PackPlan>` — PackPlan is still a type, just now a class. Compiles unchanged. |
| `src/core/archive_plan.cpp` | LIGHT | ~40 lines | `pack::selectPackPlanIndexes()` → `service.selectPackPlanIndexes()`. `pack::resolveZipNameForIndex(plan, idx)` → `plan.zipNameForIndex(idx)`. `plan.groups[index]` → `plan.groups()[index]`. |
| `src/core/archive_plan.h` | TRIVIAL | 1 line | `#include "pack/pack_service.h"` already present — PackPlan moves but is still reachable. |
| `src/app/pipeline.cpp` | TRIVIAL | ~5 lines | `buildDirectoryPackPlan()` → `packer.buildDirectoryPackPlan()`. **Facade phase: ZERO changes.** |

#### UNCHANGED Files

| File | Why Unchanged |
|------|---------------|
| `src/core/app_context.h` | No pack types used; only `jobstate::Store` forward declaration |
| `src/core/task_executor.h/.cpp` | No pack dependency; generic task execution |
| `src/core/job_state.h/.cpp` | Already a class; no pack dependency (archive_plan.cpp mediates pack ↔ job_state) |
| `src/core/parallel.h/.cpp` | Generic parallel utilities |
| `src/core/progress.h/.cpp` | Progress bar abstraction |
| `src/video/encode_config.h` | Data-only struct |
| `src/video/video_encode_runner.h/.cpp` | No pack dependency |
| `src/video/video_encoding_state.cpp` | No pack dependency |
| `src/video/video_batch_execution.cpp` | No pack dependency |
| `src/video/video_workflow_utils.h` | Template helpers only |
| `src/infra/*` | Infrastructure layer — no pack dependency |
| `src/utils/*` | General utilities |
| All test files using facade | Compile unchanged against deprecated wrappers |

### Namespace Strategy

| Namespace | Contents | Visibility |
|-----------|----------|------------|
| `pack::` | PackPlan (class), PackService (class), IPacker (interface), Packer (class), PackFileEntry (struct), FileOrdinalRange (struct), PackRunResult (struct) | Public API — consumed by video/, picture/, core/ |
| `pack::` | Utility free functions: `buildGroupOrdinalRanges()`, `appendOrdinalRangeSuffix()`, `defaultZipNameForIndex()`, `defaultProgressLabelForZipName()` | Keep as free functions — they're pure computation, no state, consumed by multiple subsystems |
| `pack::detail::` | PackGroupInput, PackGroupPartition, PackEntryInput, PackEntryPartition | Internal to pack subsystem. Not visible to video/picture. |
| Global scope → migrated | Currently global: `packFilesToZip` (3 overloads), `groupFilesBySize`, `groupPackFiles`, `groupPackFilesWithSubparts`, `groupPackEntries`, `groupPackEntriesWithSubparts`, `packAllFilesInDirectory`, `runDirectoryPackWorkflow`, `buildDirectoryPackPlan` | All move into `pack::Packer` class methods or `pack::` free functions. Global scope cleared. |

**Design decision — keep some free functions in `pack::`:** `buildGroupOrdinalRanges()` and `appendOrdinalRangeSuffix()` are pure functions with no side effects, no state, and are consumed by video and picture subsystems for naming logic. They do not belong on any class. This is consistent with "free functions are fine for pure computation" — similar to how C++ stdlib keeps `std::sort` free even though `std::vector` exists.

**Design decision — PackGroupInput etc. to `pack::detail::`:** These are input parameter types used only by `Packer`'s grouping methods. The public API for grouping accepts `std::vector<PackFileEntry>` or `std::vector<fs::path>` — the intermediate types (PackGroupInput, PackEntryInput) are implementation details. Moving them to `detail::` signals this.

### Circular Dependency Resolution

**Problem:** `packer.h` includes `pack_service.h` (for `PackFileEntry` type). After refactoring, both headers may need each other's types.

**Solution — factor shared types first:**
```
Before:
  packer.h → #include "pack/pack_service.h"  (circular with pack_service.h → includes packer.h indirectly)

After:
  pack_types.h  ← included by pack_plan.h, pack_service.h, packer.h, packer_interface.h
  pack_plan.h   ← included by pack_service.h, archive_plan.h
  packer_interface.h ← included by pack_service.h
  pack_service.h → #include pack_plan.h, packer_interface.h (NOT packer.h)
  packer.h       → #include packer_interface.h, pack_types.h (NOT pack_service.h)
```

Resolution order: Extract `pack_types.h` first, then `packer_interface.h`, then restructure includes.

## Build Order

### Phase A: Type Extraction (no behavioral changes)

1. **Create `pack_types.h`** — Move PackFileEntry, FileOrdinalRange, PackRunResult from pack_service.h to new header.
2. **Update `pack_service.h`** — Include pack_types.h instead of defining types inline.
3. **Update `packer.h`** — Include pack_types.h instead of pack_service.h. This BREAKS the circular dependency.
4. **Update `packer.cpp`** — Add `#include "pack/pack_service.h"` as needed (was indirectly included).
5. **Verify**: All existing code compiles, all 909 assertions pass. ZERO behavioral changes.

### Phase B: Create New Classes (behind facade, no consumer changes)

1. **Create `packer_interface.h`** — IPacker abstract class.
2. **Create `packer_types.h`** — Move global-scope structs into `pack::detail::`.
3. **Create `pack_plan.h/.cpp`** — PackPlan class with Builder.
4. **Rewrite `packer.h/.cpp`** — Packer class implementing IPacker, all methods. Keep old free functions as `[[deprecated]]` wrappers in pack_facade.h.
5. **Rewrite `pack_service.h/.cpp`** — PackService class. Keep old free functions as `[[deprecated]]` wrappers in pack_facade.h.
6. **Add `pack_facade.h`** — deprecated wrappers for all old free functions.
7. **Update consumers minimally** — Add `#include "pack/pack_facade.h"` where needed. ZERO other changes.
8. **Verify**: All 909 assertions pass. PackService unit tests pass with MockPacker.

### Phase C: Migrate Consumers (one subsystem at a time)

1. **Migrate video_process.cpp** — PackPlan::Builder, PackService instance.
2. **Migrate video_output_planning.cpp** — Packer::groupPackFiles.
3. **Migrate picture_process.cpp** — Both PackPlan construction sites, PackService calls.
4. **Migrate archive_plan.cpp** — PackService::selectPackPlanIndexes, PackPlan accessors.
5. **Migrate app/pipeline.cpp** — Packer::buildDirectoryPackPlan.
6. **Verify after each**: Subsystem tests pass, facade still available for unmigrated consumers.

### Phase D: Cleanup

1. **Remove `pack_facade.h`** — All consumers migrated.
2. **Remove deprecated wrappers from pack_service.h and packer.h**.
3. **Final verify**: All 909 assertions pass. No deprecated warnings.

**Rationale for this order:**
- Phase A breaks the circular dependency first (lowest risk, pure refactoring).
- Phase B establishes the new OO API without disrupting any consumer.
- Phase C migrates consumers incrementally, one subsystem per commit.
- At every step, the full test suite (909 assertions) can run and pass.

## Mockability Strategy

### For Unit Testing PackService

PackService depends on two external concerns:
1. **Zip I/O** (libzippp) — Mocked via `IPacker` interface
2. **TaskExecutor** (taskexec::runTasks) — Not mocked initially (it's core infrastructure)

```cpp
// In tests/pack/pack_service_tests.cpp
namespace {

class MockPacker : public pack::IPacker {
public:
  // Control return values for test scenarios
  eh::Result<void> packResult = {};
  std::vector<std::vector<pack::PackFileEntry>> capturedGroups;
  std::vector<std::filesystem::path> capturedZipPaths;

  auto packFilesToZip(
    std::vector<pack::PackFileEntry> const& entries,
    std::filesystem::path const& zipFilePath,
    PackEntryProgressCallback onEntryPacked,
    std::atomic<std::size_t>* finalizingCount
  ) -> eh::Result<void> override {
    capturedGroups.push_back(entries);
    capturedZipPaths.push_back(zipFilePath);
    return packResult;
  }

  auto groupFilesBySize(
    std::vector<std::filesystem::path> const& filePaths,
    std::uintmax_t maxGroupSize,
    std::optional<std::size_t> maxFilesPerGroup
  ) -> std::vector<std::vector<std::filesystem::path>> override {
    // ... recording and returning controlled groups
  }

  // ... other IPacker methods
};

} // namespace

TEST_CASE("PackService::packGroups delegates to IPacker", "[pack-service]") {
  auto mockPacker = std::make_unique<MockPacker>();
  mockPacker->packResult = {}; // success

  pack::PackPlan plan = pack::PackPlan{pack::PackPlan::Builder{
    .groups = {/* test groups */},
    .outputDir = "/tmp/out",
    .compact = false,
  }};

  pack::PackService service(std::move(mockPacker));
  auto result = service.packGroups(plan);

  REQUIRE(result);
}
```

### What Gets Mocked vs Not

| Component | Mock? | Rationale |
|-----------|:-----:|-----------|
| IPacker (zip I/O) | YES | External dependency (libzippp); enables fast unit tests without filesystem |
| PackPlan | NO | Pure data; construct real instances in tests |
| PackService | NO | Unit under test; inject MockPacker |
| TaskExecutor | NO (initially) | Core infrastructure; tested via integration |
| jobstate::Store | Partial | Already a class; can be tested with temp files |
| ProgressContext | NO | indicators library wrapper; tested via integration |

## Scaling Considerations

| Concern | Current Approach | After Refactoring |
|---------|-----------------|-------------------|
| Compile times | Single glob `src/**.cpp` — all files recompile on any header change | pack_types.h extracted → changes to PackPlan internals don't trigger picture/video recompilation |
| Test isolation | Tests link against all `src/**.cpp|main.cpp` — can't mock zip I/O | MockPacker enables pack_service tests without libzippp or filesystem |
| Binary size | All symbols in single binary | No change — inline/deprecated wrappers removed in Phase D |
| New packer backend | Hard-coded libzippp | Implement new IPacker subclass (e.g., `MinizPacker`), inject into PackService |

## Anti-Patterns to Avoid

### Anti-Pattern 1: God Class PackService

**What people do:** Dump all pack functionality (zip I/O, grouping, naming, progress, resumability) into a single PackService class with 30+ methods.

**Why it's wrong:** Violates Single Responsibility. Makes PackService untestable without real zip I/O. Makes progress/compact logic inseparable from zip operations.

**Do this instead:** Split into Packer (zip I/O + grouping), PackPlan (data model), PackService (orchestration + progress + resumability). Three focused classes, not one god class.

### Anti-Pattern 2: Breaking All Consumers Atomically

**What people do:** Rewrite pack_service.h, packer.h, AND all 6 consumer files in a single commit.

**Why it's wrong:** High risk — one compilation error anywhere blocks all progress. Hard to bisect. Tests can't run until everything compiles.

**Do this instead:** Facade pattern. New classes live alongside (not instead of) old free functions via deprecated wrappers. Migrate consumers one at a time.

### Anti-Pattern 3: Over-Engineering the Builder

**What people do:** Create a multi-step fluent builder with `.setGroups().setOutputDir().validate().build()` chain for PackPlan.

**Why it's wrong:** PackPlan has only ~6 meaningful fields. A builder with separate setter for each field adds 40+ lines of boilerplate for no benefit over a simple constructor struct.

**Do this instead:** Single `PackPlan::Builder` struct passed to constructor. Preserves designated-initializer readability with one level of nesting:

```cpp
// Good — clean, readable, minimal boilerplate
auto plan = pack::PackPlan{pack::PackPlan::Builder{
  .groups = groupedEntries,
  .outputDir = zipOutputDir,
  .zipNameForIndex = [&](std::size_t i) { return std::format("part{}.zip", i); },
  .maxParallelJobs = ctx.config.maxParallelJobs,
  .compact = !ctx.config.fullProgress,
}};

// Bad — over-engineered
auto plan = pack::PackPlan::builder()
  .withGroups(groupedEntries)
  .withOutputDir(zipOutputDir)
  .withZipNaming([&](auto i) { return std::format("part{}.zip", i); })
  .withMaxParallelJobs(ctx.config.maxParallelJobs)
  .withCompactMode(!ctx.config.fullProgress)
  .build();
```

### Anti-Pattern 4: Making Everything a Class

**What people do:** Convert every struct (`PackFileEntry`, `FileOrdinalRange`, `PackRunResult`) to a class with getters/setters.

**Why it's wrong:** These are value types with zero invariants. Adding getters/setters for `sourcePath`, `zipEntryName` just creates busy-work and breaks existing aggregate initialization in tests.

**Do this instead:** Keep value types as structs with public members. Only `PackPlan` (has invariants, callbacks, construction complexity) and the service classes (have dependencies, state) become classes.

## Sources

- `src/pack/pack_service.h` — PackPlan struct, free functions, static_assert guard (HIGH confidence — primary source)
- `src/pack/pack_service.cpp` — PackPlan orchestration, CompactProgressState, anonymous namespace helpers (HIGH confidence — primary source)
- `src/pack/packer.h` — Global-scope structs, free function declarations, circular include of pack_service.h (HIGH confidence — primary source)
- `src/pack/packer.cpp` — Zip I/O, file grouping, anonymous namespace helpers (HIGH confidence — primary source)
- `src/video/video_process.cpp:395-448` — PackPlan construction via designated initializer (HIGH confidence — primary source)
- `src/video/video_output_planning.cpp:175-203` — Uses global-scope groupPackFiles (HIGH confidence — primary source)
- `src/picture/picture_process.cpp:150-198, 607-615` — PackPlan construction sites × 2, pack::runPackPlan/packGroups calls (HIGH confidence — primary source)
- `src/picture/picture_process.h:27,34` — Functions returning `pack::PackPlan` (HIGH confidence — primary source)
- `src/core/archive_plan.h` — `PreparedPackExecution` struct, depends on `pack::PackPlan` (HIGH confidence — primary source)
- `src/core/archive_plan.cpp:26-89` — Uses `pack::resolveZipNameForIndex`, `pack::selectPackPlanIndexes`, accesses `plan.groups[index]` (HIGH confidence — primary source)
- `src/core/job_state.h:72-129` — `jobstate::Store` as existing OO class precedent (HIGH confidence — primary source)
- `xmake.lua:38-50,59-76` — Build system: glob patterns, test linking (HIGH confidence — primary source)
- `.planning/PROJECT.md:27-36` — v1.3 milestone requirements (HIGH confidence — authoritative)
- `.planning/PROJECT.md:90-98` — Current architecture decisions (HIGH confidence — authoritative)
- `tests/pack_service_tests.cpp` — Test patterns: direct PackPlan construction, designated initializers (HIGH confidence — primary source)
- `tests/packer_tests.cpp` — Test patterns: global-scope function calls (HIGH confidence — primary source)
- Existing `videobatch::detail` namespace precedent in `video_batch_execution.h` for shared struct definitions (HIGH confidence — v1.2 decision pattern)

---

*Architecture research for: encro v1.3 Pack Subsystem OO Refactor*
*Researched: 2026-04-29*
