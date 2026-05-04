# Architecture Research

**Domain:** Naming strategy abstraction + grouping config + summary toggle integration into encrō pack module
**Researched:** 2026-05-04
**Confidence:** HIGH (all findings verified against actual source code)

## System Overview (Current v1.4)

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                          Consumers (v1.4)                                      │
├──────────────────────────────────────────────────────────────────────────────┤
│  ┌──────────────────┐  ┌──────────────────┐  ┌────────────────────────────┐  │
│  │ pipeline.cpp     │  │ video_process.cpp│  │ picture_process.cpp        │  │
│  │ (PackReq→execute)│  │ (PackReq→execute)│  │ (PackPlan→execute) ⚠ LEAK │  │
│  └────────┬─────────┘  └────────┬─────────┘  └────────────┬───────────────┘  │
│           │                     │                          │                  │
│           └─────────────────────┼──────────────────────────┘                  │
│                                 │                                             │
│                      pack::execute(PackRequest)                                │
│                                 │                                             │
├─────────────────────────────────┼─────────────────────────────────────────────┤
│                         pack.h (PUBLIC)                                        │
│  ┌──────────────────────────────────────────────────────────────────────────┐ │
│  │ PackRequest │ PackMode │ NamingConfig │ PackRunResult │ execute()        │ │
│  └──────────────────────────────────────────────────────────────────────────┘ │
├──────────────────────────────────────────────────────────────────────────────┤
│                      pack.cpp (INTERNAL)                                       │
│  ┌─────────────────────┐  ┌─────────────────────┐                             │
│  │ buildMediaPackPlan()│  │ runResumable()      │                             │
│  │ (grouping, naming,  │  │ (job state merge,   │                             │
│  │  PackPlan assembly) │  │  callback wiring,   │                             │
│  └──────────┬──────────┘  │  filtered execution)│                             │
│             │              └──────────┬──────────┘                             │
│             └──────────┬─────────────┘                                         │
│                        │                                                       │
│             PackService (orchestration, owns Packer by value)                  │
│                        │                                                       │
│             Packer (zip I/O, grouping algorithms)                              │
├──────────────────────────────────────────────────────────────────────────────┤
│                    pack_internal.h (DEMOTED HELPERS)                           │
│  buildGroupOrdinalRanges │ appendOrdinalRangeSuffix │ resolveZipNameForIndex  │
│  selectPackPlanIndexes                                                         │
├──────────────────────────────────────────────────────────────────────────────┤
│                   pack_types.h / packer_types.h                                 │
│  PackFileEntry │ PackEntryInput │ FileOrdinalRange │ PackPlan │               │
│  pack::detail::PackEntryInput │ PackGroupInput │ PackEntryPartition           │
└──────────────────────────────────────────────────────────────────────────────┘

⚠ LEAK: picture_process.cpp includes pack_internal.h, packer.h, packer_types.h,
  uses pack::detail::PackEntryInput, builds PackPlan directly, calls execute(PackPlan).
  This is the SINK-03 target.
```

## Current Dependencies

## Component Responsibilities (Current)

| Component | Responsibility | File(s) |
|-----------|----------------|---------|
| `pack::PackRequest` | Declarative intent — entries, mode, naming, progress flags | `pack.h` |
| `pack::NamingConfig` | Optional sub-struct: layout, forceConflictHandling, baseName, zipNameStrategy callback | `pack.h` |
| `pack::execute()` | Free function entry point; builds PackPlan from PackRequest; dispatches to runNonResumable/runResumable | `pack.cpp` |
| `buildMediaPackPlan()` | Anonymous-namespace function: groups entries → two-layer partition → applies naming → returns PackPlan | `pack.cpp` (anon ns) |
| `Packer::groupPackEntriesWithSubparts()` | Physical grouping: source-dir-aware partitioning into PackEntryPartition | `packer.h/cpp` |
| `Packer::buildDirectoryPackPlan()` | Directory-mode plan builder (raw file tree → entries with conflict handling) | `packer.h/cpp` |
| `pack::internal::` | Demoted static helpers: ordinal ranges, suffix appending, zip name resolution | `pack_internal.h` |
| `collisionnaming::` | Inline utility functions: path normalization, hashing, sanitization, conflict-handled flat names | `collision_naming.h` |
| `picture_process.cpp` | Picture workflow: scans pics, plans entry names, builds PackEntryInput vectors, constructs PackPlan via `buildPicturePackPlan()`, calls `execute(PackPlan)` | `src/picture/` |

### picture_process.cpp Internal Leak (SINK-03 Target)

picture_process.cpp currently has **5 internal pack includes** that must be eliminated:

```
#include "pack/pack_internal.h"     ← pack::internal::
#include "pack/packer.h"            ← pack::Packer class
#include "pack/packer_types.h"      ← pack::detail:: types
```

And uses these internal types:
- `pack::detail::PackEntryInput` (aliased in `packer_types.h` from `pack::PackEntryInput`)
- `pack::PackPlan` (constructed directly, then passed to `pack::execute(PackPlan)`)
- `pack::Packer` (instantiated to call `groupPackEntries()`)
- `pack::FileOrdinalRange` (used in `PicturePackNamingState`)
- `pack::internal::buildGroupOrdinalRanges()`, `pack::internal::appendOrdinalRangeSuffix()`

**What picture_process does that must migrate:**
1. Calls `planPictureZipEntryNames()` — custom entry name planning (Keep/Flat/Flat+Force logic)
2. Calls `buildPicturePackEntryInputs()` — makes `pack::detail::PackEntryInput` vectors with "0000__"/"1000__" sourceKey prefixes
3. Calls `buildPictureLogicalBuckets()` → `buildPictureLogicalParts()` — logical grouping (dir-containment, kMaxPicturesPerPack = 2000)
4. Constructs `Packer` directly, calls `groupPackEntries()` for physical grouping
5. Calls `buildPicturePackPlan()` — assembles PackPlan with zipNameForIndex
6. Calls `pack::execute(PackPlan, jobState)` — the internal plan overload

**Target state (SINK-03):** picture_process builds a `PackRequest` with entries (file paths or PackEntryInputs), naming config, grouping strategy, and summary toggle — calls `pack::execute(PackRequest)` only.

## Current Dependencies

```
AppConfig
  ├── outputLayout (OutputLayout::Flat | Keep)     ← needed by picture naming
  ├── forceNameConflictHandling (bool)              ← needed by picture naming
  ├── fullProgress (bool)                           ← needed by compact flag
  ├── pictureFolderSummary (bool)                   ← needed by summary feature
  ├── maxParallelJobs                               ← needed by PackPlan
  └── compressImages / imageQuality                 ← picture-only, stays

NamingConfig (pack.h)
  ├── layout (OutputLayout)                         ← mirrors AppConfig
  ├── forceConflictHandling (bool)                  ← mirrors AppConfig
  ├── baseName (optional<string>)                   ← for zip file naming
  └── zipNameStrategy (callback)                    ← consumer-provided custom naming

PackRequest (pack.h)
  ├── entries (vector<path>)                        ← file paths
  ├── entryInputs (vector<PackEntryInput>)          ← explicit entries with grouping keys
  ├── mode (PackMode)                               ← Media | Directory
  ├── outputDir (path)                              ← where to write zips
  ├── compact (bool)                                ← from !config.fullProgress
  ├── removeOnFailure (bool)
  ├── naming (optional<NamingConfig>)
  ├── maxParallelJobs (optional<size_t>)
  ├── recursive (bool)                              ← directory mode only
  ├── jobState (Store*)                             ← non-null = resumable
  └── entryNameForFile (callback)                   ← optional path→name mapping
```

### Naming Mode Matrix (What Exists)

| outputLayout | forceConflictHandling | Behavior | Used By |
|-------------|----------------------|----------|---------|
| `Flat` | `false` | Flatten filename with "1000__" prefix | picture non-force path |
| `Flat` | `true` | Flatten with hash-disambiguated conflict handling | picture force path, directory pack |
| `Keep` | (ignored) | Preserve directory structure relative to root | picture keep-layout path |

**Problem:** Two separate fields (`layout` + `forceConflictHandling`) encode 3 behaviors where the boolean is irrelevant for Keep. The "1000__" prefix is hardcoded into picture_process.cpp, not configurable. The collision-based flattening spans two files (collision_naming.h for hash helpers, picture_process.cpp for orchestration, pack.cpp for directory).

---

## Recommended Architecture (v1.5 Target)

### System Overview (Post-Migration)

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                          Consumers (v1.5 — UNIFIED)                            │
├──────────────────────────────────────────────────────────────────────────────┤
│  ┌──────────────────┐  ┌──────────────────┐  ┌────────────────────────────┐  │
│  │ pipeline.cpp     │  │ video_process.cpp│  │ picture_process.cpp        │  │
│  │ (PackReq→execute)│  │ (PackReq→execute)│  │ (PackReq→execute) ✓ FIXED │  │
│  └────────┬─────────┘  └────────┬─────────┘  └────────────┬───────────────┘  │
│           │                     │                          │                  │
│           └─────────────────────┼──────────────────────────┘                  │
│                                 │                                             │
│                      pack::execute(PackRequest)                                │
│                      pack::execute(PackPlan, Store*) ← INTERNAL ONLY          │
│                                 │                                             │
├─────────────────────────────────┼─────────────────────────────────────────────┤
│                    pack.h (PUBLIC — EXPANDED)                                  │
│  ┌──────────────────────────────────────────────────────────────────────────┐ │
│  │ PackRequest │ PackMode │ NamingConfig │ NamingStrategy enum │            │ │
│  │ GroupingStrategy enum │ SummaryConfig │ PackRunResult │ execute()         │ │
│  └──────────────────────────────────────────────────────────────────────────┘ │
├──────────────────────────────────────────────────────────────────────────────┤
│                    pack.cpp (INTERNAL — EXPANDED)                              │
│  ┌──────────────────────────────────────────────────────────────────────────┐ │
│  │ buildMediaPackPlan() — now handles picture path too                      │ │
│  │   • Naming dispatch via NamingStrategy enum (not bool+layout)            │ │
│  │   • Summary entry injection when SummaryConfig enabled                   │ │
│  │   • Grouping dispatch via GroupingStrategy enum (not implicit)           │ │
│  │ buildDirectoryPackPlan() — uses new NamingStrategy too                   │ │
│  │ runResumable() — unchanged                                               │ │
│  └──────────────────────────────────────────────────────────────────────────┘ │
│                                                                               │
│             PackService / Packer — unchanged (no API changes)                 │
├──────────────────────────────────────────────────────────────────────────────┤
│      collision_naming.h → pack_naming.h (refactored, internal to pack/)       │
│  buildCollisionGroupPrefix │ buildConflictHandledFlatName                     │
│  (sanitizeLabel, shortPathHash, stablePathString — stay as is)                │
└──────────────────────────────────────────────────────────────────────────────┘

picture_process.cpp now:
  - Scans pics, plans compress tasks if needed
  - Builds a PackRequest with:
      entries[] via entryInputs (summary + regular)
      groupingStrategy = LogicalPerSourceDir
      naming = NamingConfig{ .strategy = NamingStrategy::FlatWithForce, .prefix = "1000__" }
      summaryConfig = SummaryConfig{ .enabled = config.pictureFolderSummary }
  - Calls pack::execute(request)
  - All PackEntryInput, PackPlan, Packer, pack::internal:: gone from includes
```

### Component Responsibilities (Target)

| Component | Responsibility | File(s) | New/Modified |
|-----------|----------------|---------|--------------|
| `pack::NamingStrategy` | Enum replacing outputLayout+forceConflictHandling: `Flat`, `FlatWithForce`, `Keep` | `pack.h` | **NEW** |
| `pack::GroupingStrategy` | Enum for grouping policy: `PerSourceDir` (default), `SingleGroup`, `LogicalPerSourceDir` (picture) | `pack.h` | **NEW** |
| `pack::SummaryConfig` | Sub-struct for summary picture injection: `enabled`, `prefix` ("0000__" default) | `pack.h` | **NEW** |
| `pack::NamingConfig` | Simplified: replaces `layout`+`forceConflictHandling` with `strategy` (NamingStrategy enum), adds optional `prefix` | `pack.h` | **MODIFIED** |
| `pack::PackRequest` | Adds `groupingStrategy`, `naming` now uses simplified NamingConfig, adds `summary` (optional SummaryConfig) | `pack.h` | **MODIFIED** |
| `pack::execute()` | Unchanged signature. Internal dispatch now handles new fields | `pack.cpp` | **MODIFIED** (internal only) |
| `buildMediaPackPlan()` | Now handles picture path (summary injection, logical grouping), naming dispatch via enum | `pack.cpp` (anon ns) | **MODIFIED** |
| `picture_process.cpp` | Removes 5 internal pack includes, replaces plan construction with PackRequest construction | `src/picture/` | **MODIFIED** |
| `pipeline.cpp` | Adds naming strategy field to PackRequest (was using layout+forceConflictHandling) | `src/app/` | **MODIFIED** (minor) |
| `collision_naming.h` | Unchanged (inline utilities stay where they are; used by picture compress path, naming logic) | `src/core/` | **UNCHANGED** |
| `pack::internal::` | Unchanged (ordinal range helpers don't change) | `pack_internal.h` | **UNCHANGED** |

---

## Recommended Project Structure

```
src/
├── pack/
│   ├── pack.h                  # PUBLIC: PackRequest, NamingStrategy, GroupingStrategy,
│   │                            #         SummaryConfig, NamingConfig, PackMode,
│   │                            #         PackRunResult, execute() declaration
│   ├── pack.cpp                # INTERNAL: execute(PackRequest), buildMediaPackPlan(),
│   │                            #           runNonResumable, runResumable
│   ├── pack_types.h            # PackFileEntry, PackEntryInput, FileOrdinalRange,
│   │                            #   PackPlan (pure internal), PackProgressCallbacks
│   ├── packer_types.h          # pack::detail:: PackGroupInput, PackEntryPartition, etc.
│   │                            #   → NOT included by consumers
│   ├── packer.h                # Packer class → INTERNAL only
│   ├── packer.cpp              # Packer implementation
│   ├── pack_service.h          # PackService → INTERNAL only
│   ├── pack_service.cpp        # PackService implementation
│   └── pack_internal.h         # Demoted helpers → INTERNAL only
│
├── picture/
│   ├── picture_process.h       # runPicturePackWorkflow, packAllPicsToZip
│   └── picture_process.cpp     # NOW: builds PackRequest, calls pack::execute(PackRequest)
│                                # GONE: pack_internal.h, packer.h, packer_types.h includes
│                                # GONE: buildPicturePackPlan, buildPictureLogicalParts,
│                                #        buildPictureLogicalBuckets, PicturePackNamingState
│
├── app/
│   └── pipeline.cpp            # UPDATED: NamingConfig uses NamingStrategy enum
│
├── video/
│   └── video_process.cpp       # Unchanged (already uses PackRequest)
│
└── core/
    └── collision_naming.h      # Unchanged (utilities used by picture compress)
```

### Structure Rationale

- **`pack.h` gets all new public types:** Following v1.4's "single public header" pattern. Consumers never include pack_types.h, packer_types.h, pack_service.h, or packer.h. This is verified by `pack_api_standalone_compile_test.cpp`.
- **`collision_naming.h` stays in `core/`:** Its inline utilities (sanitizeLabel, shortPathHash, etc.) are used by picture compression path (`buildCompressTaskKey`), not just by pack subsystem. Moving it to `pack/` would create a new dependency direction.
- **No new files needed:** The new enums and structs fit naturally into the existing `pack.h`. No new compilation units.

---

## Architectural Patterns

### Pattern 1: Strategy Enum Dispatch (Replaces bool+enum Combo)

**What:** Replace `NamingConfig`'s `OutputLayout layout` + `bool forceConflictHandling` pair with a single `NamingStrategy` enum that has one value per behavior.

**Current (v1.4) — 3-way behavior encoded in 2 fields:**
```cpp
struct NamingConfig {
  appctx::OutputLayout layout;       // Flat or Keep
  bool forceConflictHandling = false; // only meaningful when layout==Flat
  std::optional<std::string> baseName;
  std::function<...> zipNameStrategy;
};
// Usage pattern: switch on (layout, forceConflictHandling) pair
```

**Target (v1.5) — 3-way enum:**
```cpp
enum class NamingStrategy {
  Flat,           // Flatten filenames, no conflict disambiguation
  FlatWithForce,  // Flatten with hash-based conflict disambiguation
  Keep,           // Preserve relative directory structure (zip entry names mirror tree)
};

struct NamingConfig {
  NamingStrategy strategy = NamingStrategy::Flat;
  std::optional<std::string> entryPrefix;  // e.g. "1000__" for regular, "0000__" for summary
  std::optional<std::string> baseName;
  std::function<std::string(std::size_t, std::size_t, std::size_t, std::string_view, FileOrdinalRange)>
    zipNameStrategy;  // unchanged — consumer-provided custom naming
};
```

**When to use:** When a boolean flag only makes sense in combination with one enum value. The `forceConflictHandling` bool was dead weight for `Keep` layout. A 3-value enum eliminates the invalid state.

**Trade-offs:**
- PRO: Impossible to represent invalid states (e.g., `{Keep, forceConflictHandling=true}`)
- PRO: Single `switch` dispatch instead of nested `if/switch`
- PRO: Makes the `NamingStrategy` enum self-documenting
- CON: Breaking change to `NamingConfig` struct — all construction sites must update
- CON: `AppConfig` still stores `OutputLayout` and `forceConflictHandling` — consumers must translate to NamingStrategy

**Mapping from AppConfig → NamingStrategy:**
```
OutputLayout::Flat + forceConflictHandling=false  → NamingStrategy::Flat
OutputLayout::Flat + forceConflictHandling=true   → NamingStrategy::FlatWithForce
OutputLayout::Keep + (any)                         → NamingStrategy::Keep
```

### Pattern 2: Summary Injection via Sub-Config

**What:** Picture "folder summary" feature injects one summary entry per source directory at the front of each logical grouping. Currently picture_process.cpp does this manually with "0000__" key prefix. Abstract into a `SummaryConfig` sub-struct that tells `buildMediaPackPlan()` to inject summary entries.

**Current (v1.4) — picture_process owns summary logic:**
```cpp
// picture_process.cpp
if (config.pictureFolderSummary) {
  summaryPics = collectFolderSummaryPictures(dirPath, scannedPics);
}
// summary entries get sourceKey = "0000__" prefix
// regular entries get sourceKey = "1000__" prefix
```

**Target (v1.5) — summary as PackRequest concern:**
```cpp
struct SummaryConfig {
  bool enabled = false;
  std::string entryPrefix = "0000__";  // prefix for summary entry grouping keys
  std::string regularPrefix = "1000__"; // prefix for regular entry grouping keys
};

struct PackRequest {
  // ... existing fields ...
  std::optional<SummaryConfig> summary;  // nullopt = no summary injection
  // ...
};
```

**When to use:** When a cross-cutting concern (summary entries) affects grouping, naming, and entry injection — all of which happen inside `buildMediaPackPlan()`. By making it a PackRequest field, the internal function can inject summary entries without the consumer managing it.

**Trade-offs:**
- PRO: picture_process no longer needs to build separate summary+regular entry vectors
- PRO: summary injection is testable through the pack::execute() API boundary
- CON: SummaryConfig is picture-specific, but lives in the general pack.h header (acceptable — Directory mode just ignores it)

### Pattern 3: Grouping Strategy Enum (Consumer Declares Intent)

**What:** Currently grouping is implicit: `buildMediaPackPlan()` always uses `groupPackEntriesWithSubparts()` with kMaxEntriesPerPart=2000. Picture has an extra logical grouping layer (`buildPictureLogicalParts`). Make the grouping policy a declared enum field on PackRequest.

**Values:**
```cpp
enum class GroupingStrategy {
  PerSourceDir,          // Group by source directory (default — current Media mode behavior)
  PerSourceDirKeepTogether, // Keep source dir entries together, split at logical boundary (picture mode)
  SingleGroup,           // Put everything in one group (for tiny inputs)
};
```

**When to use:** When different consumers need different grouping policies for the same `PackMode::Media`. The picture workflow needs `PerSourceDirKeepTogether` (logical parts) while video uses `PerSourceDir` (simple partitioning).

**Trade-offs:**
- PRO: Consumer intent is explicit, not buried in implicit logic
- PRO: `buildMediaPackPlan()` can switch on the enum instead of having separate code paths
- CON: Enum values must be stable — adding a new strategy requires understanding all grouping algorithms
- CON: The `keepSourceDirsTogetherWhenTotalFilesExceed` parameter on Packer methods maps to this — need to ensure correspondence

---

## Data Flow

### New PackRequest → execute() Flow

```
picture_process / video_process / pipeline
    │
    │  Constructs PackRequest with:
    │    .entries or .entryInputs
    │    .mode = PackMode::Media
    │    .naming = NamingConfig { .strategy = NamingStrategy::..., .entryPrefix = "..." }
    │    .groupingStrategy = GroupingStrategy::...
    │    .summary = SummaryConfig { .enabled = true/false }
    │    .compact = ...
    │    .outputDir = ...
    │    .maxParallelJobs = ...
    │
    ▼
pack::execute(PackRequest)
    │
    ├── PackMode::Directory ──→ Packer::buildDirectoryPackPlan()
    │                           (uses naming.strategy for conflict handling)
    │
    └── PackMode::Media ──→ buildMediaPackPlan(request)
                              │
                              ├── 1. Convert entries → PackEntryInput[] (if not explicit)
                              │
                              ├── 2. IF summary.enabled:
                              │       Inject summary entries with entryPrefix prefix
                              │       into the PackEntryInput set
                              │
                              ├── 3. SWITCH on groupingStrategy:
                              │       PerSourceDir:          groupPackEntriesWithSubparts()
                              │       PerSourceDirKeepTogether: logicalParts → groupPackEntries()
                              │       SingleGroup:           single group, no partitioning
                              │
                              ├── 4. SWITCH on naming.strategy:
                              │       Flat:          entry name = prefix + filename
                              │       FlatWithForce: entry name = prefix + collision-handled flat name
                              │       Keep:          entry name = relative path
                              │
                              ├── 5. Apply entryNameForFile if consumer-provided
                              │
                              ├── 6. Build zipNameForIndex lambda
                              │
                              └── 7. Return PackPlan

    │
    ▼
execute(PackPlan, jobState)
    │
    ├── jobState==null → runNonResumable → PackService::packGroups → Packer::packFilesToZip
    └── jobState!=null → runResumable   → mergeTasks → filter → PackService::packGroups
```

### picture_process Migration Flow

```
BEFORE (v1.4):
  picture_process.cpp
    │
    ├── readAllPics() → scannedPics[]
    ├── planPictureZipEntryNames() → entryName map
    ├── buildPicturePackEntryInputs() → pack::detail::PackEntryInput[]
    │     └── requires packer_types.h, collision_naming.h
    ├── buildPictureLogicalParts() → logical part vectors
    │     └── buildPictureLogicalBuckets() → sort, group by sourceKey
    ├── Packer::groupPackEntries() → physical groups
    │     └── requires packer.h
    ├── buildPicturePackPlan() → PackPlan
    │     └── requires pack_internal.h (ordinal ranges)
    │     └── requires pack_types.h (PackPlan, FileOrdinalRange)
    └── pack::execute(PackPlan, jobState)
          └── requires pack.h (PackPlan overload)

AFTER (v1.5):
  picture_process.cpp
    │
    ├── readAllPics() → scannedPics[]
    ├── planPictureZipEntryNames() → entryName map (stays — naming is still picture's concern
    │     for compression output paths, not pack entry names)
    ├── Build PackRequest:
    │     .entryInputs = scannedPics mapped to PackEntryInput (via entryName map)
    │     .naming.strategy = map from AppConfig.layout + forceConflictHandling
    │     .naming.entryPrefix = "1000__"  // regular entries
    │     .groupingStrategy = GroupingStrategy::PerSourceDirKeepTogether
    │     .summary.enabled = config.pictureFolderSummary
    │     .summary.entryPrefix = "0000__"
    │     .summary.regularPrefix = "1000__"
    │     .outputDir = ...
    │     .compact = !config.fullProgress
    │
    └── pack::execute(PackRequest)
          └── requires ONLY pack.h
```

### Key Data Flows

1. **Entry name planning stays in picture_process:** The `planPictureZipEntryNames()` function is NOT migrated. It determines entry names for compression output files (temp dir paths), which is picture-specific logic. Only the pack-level naming (zip entry names within archives) moves into pack.cpp.

2. **Summary entry injection moves into pack.cpp:** `collectFolderSummaryPictures()` stays in picture_process (it depends on scanned picture files), but the injection of summary entries into the PackEntryInput set moves into `buildMediaPackPlan()` via the SummaryConfig flag.

3. **Logical grouping moves into pack.cpp:** `buildPictureLogicalBuckets()` and `buildPictureLogicalParts()` logic migrates into `buildMediaPackPlan()` behind the `GroupingStrategy::PerSourceDirKeepTogether` enum value.

---

## Integration Points

### New Types — Where They Go

| Type | File | Visibility | Rationale |
|------|------|-----------|-----------|
| `NamingStrategy` enum | `pack.h` → `namespace pack` | PUBLIC | Replaces `OutputLayout` + `forceConflictHandling` in NamingConfig; consumers need it to construct NamingConfig |
| `GroupingStrategy` enum | `pack.h` → `namespace pack` | PUBLIC | Field on PackRequest; consumers declare intent |
| `SummaryConfig` struct | `pack.h` → `namespace pack` | PUBLIC | Optional sub-struct on PackRequest; picture consumer sets it |
| Modified `NamingConfig` | `pack.h` → `namespace pack` | PUBLIC | Drops `layout`/`forceConflictHandling`, adds `strategy`+`entryPrefix` |
| Modified `PackRequest` | `pack.h` → `namespace pack` | PUBLIC | Adds `groupingStrategy` and `summary` fields |

**None of the new types go into `pack_types.h` or `pack_internal.h`.** Those headers are for types that are invisible to consumers (PackPlan, PackFileEntry, internal helpers). The new types are all consumer-facing declarations.

### NamingConfig Evolution

```cpp
// BEFORE (v1.4) — two fields encoding 3 behaviors
struct NamingConfig {
  appctx::OutputLayout layout;
  bool forceConflictHandling = false;
  std::optional<std::string> baseName;
  std::function<std::string(std::size_t, std::size_t, std::size_t, std::string_view, FileOrdinalRange)>
    zipNameStrategy;
};

// AFTER (v1.5) — single enum encoding 3 behaviors + configurable prefix
struct NamingConfig {
  NamingStrategy strategy = NamingStrategy::Flat;
  std::optional<std::string> entryPrefix;   // e.g. "1000__" — prepended to flat entry names
  std::optional<std::string> baseName;
  std::function<std::string(std::size_t, std::size_t, std::size_t, std::string_view, FileOrdinalRange)>
    zipNameStrategy;  // unchanged
};
```

### PackRequest Evolution

```cpp
// BEFORE (v1.4)
struct PackRequest {
  std::vector<fs::path> entries;
  std::vector<PackEntryInput> entryInputs;
  PackMode mode = PackMode::Media;
  fs::path outputDir;
  bool compact = true;
  bool removeOnFailure = false;
  std::optional<NamingConfig> naming;
  std::optional<std::size_t> maxParallelJobs;
  bool recursive = true;
  jobstate::Store* jobState = nullptr;
  std::function<std::string(fs::path const&)> entryNameForFile;
};

// AFTER (v1.5)
struct PackRequest {
  std::vector<fs::path> entries;
  std::vector<PackEntryInput> entryInputs;
  PackMode mode = PackMode::Media;
  fs::path outputDir;
  bool compact = true;
  bool removeOnFailure = false;
  std::optional<NamingConfig> naming;
  GroupingStrategy groupingStrategy = GroupingStrategy::PerSourceDir;  // NEW
  std::optional<SummaryConfig> summary;                                 // NEW
  std::optional<std::size_t> maxParallelJobs;
  bool recursive = true;
  jobstate::Store* jobState = nullptr;
  std::function<std::string(fs::path const&)> entryNameForFile;
};
```

### Consumer Migration Impact

| Consumer | Current | Target | Risk |
|----------|---------|--------|------|
| `pipeline.cpp` | Uses `NamingConfig{ .layout, .forceConflictHandling }` | Change to `NamingConfig{ .strategy = NamingStrategy::... }` | LOW — 2-field change, test covers compile |
| `video_process.cpp` | No NamingConfig, plain PackRequest | Unchanged (nullopt naming = defaults) | NONE |
| `picture_process.cpp` (non-compress) | Builds PackPlan, calls `execute(PackPlan)` | Builds PackRequest, calls `execute(PackRequest)` | HIGH — major restructure, but naming/logic preserved |
| `picture_process.cpp` (compress) | Builds PackPlan, calls `execute(PackPlan)` | Builds PackRequest, calls `execute(PackRequest)` | HIGH — same as above |
| `packAllPicsToZip()` | Standalone path, builds PackPlan | Builds PackRequest, calls `execute(PackRequest)` | MEDIUM — less complex than compress path |

### Dependencies Between New Types

```
NamingStrategy enum
    ↓ (required by)
NamingConfig struct
    ↓ (required by)
PackRequest struct
    ↑ (also requires)
GroupingStrategy enum (independent — parallel)
SummaryConfig struct (independent — parallel)
```

**Build order implication:** NamingStrategy must be defined before NamingConfig, which must be defined before PackRequest. GroupingStrategy and SummaryConfig can be added in parallel or in any order relative to NamingStrategy.

---

## Build Order Recommendation

### Phase 1: Naming Strategy Enum + NamingConfig Migration (SINK-01)

**Rationale:** NamingStrategy is the foundation. Everything else builds on it. Must exist before NamingConfig can use it, and NamingConfig must exist before PackRequest can be extended.

**Changes:**
1. Add `NamingStrategy` enum to `pack.h` (before `NamingConfig`)
2. Modify `NamingConfig`: drop `layout`+`forceConflictHandling`, add `strategy`+`entryPrefix`
3. Update `pack.cpp` `buildMediaPackPlan()`: switch on `strategy` for entry name generation
4. Update `packer.cpp` `buildDirectoryPackPlan()`: use new `strategy` field
5. Update `pipeline.cpp`: translate `AppConfig` → `NamingStrategy`, use new fields
6. Update `NamingConfig` at `pack_service.h:31` (the forceNameConflictHandling parameter on `packAllFilesInDirectory`)

**Deferred:** picture_process migration (Phase 3) — Phase 1 only changes internal dispatch, not consumers.

### Phase 2: GroupingStrategy + SummaryConfig (SINK-02)

**Rationale:** These are PackRequest extensions that don't depend on SINK-01's completion (except for needing the `pack.h` header). Can potentially be done in parallel with Phase 1, but safer to sequence after.

**Changes:**
1. Add `GroupingStrategy` enum to `pack.h`
2. Add `SummaryConfig` struct to `pack.h`
3. Add `groupingStrategy` and `summary` fields to `PackRequest`
4. Update `buildMediaPackPlan()`: dispatch grouping by `groupingStrategy` enum; inject summary entries when `summary.enabled`
5. Port `buildPictureLogicalBuckets()`/`buildPictureLogicalParts()` logic into `buildMediaPackPlan()` behind the `PerSourceDirKeepTogether` strategy value
6. Port `collectFolderSummaryPictures()` → summary injection logic enters `buildMediaPackPlan()`

**Risk:** Porting picture's logical grouping logic requires careful preservation of the kMaxPicturesPerPack=2000 limit, the source-key-based sorting, and the summary-entries-fit-first-physical-pack validation.

### Phase 3: Picture Process Leak Elimination (SINK-03)

**Rationale:** Only possible after Phase 1+2 complete, because picture_process needs to construct PackRequest with NamingStrategy, GroupingStrategy, and SummaryConfig.

**Changes:**
1. In `picture_process.cpp`:
   - Remove `#include "pack/pack_internal.h"`, `#include "pack/packer.h"`, `#include "pack/packer_types.h"`
   - Replace `buildPicturePackPlan()` calls with `pack::execute(PackRequest{...})`
   - Build PackRequest with `.entryInputs`, `.naming.strategy`, `.groupingStrategy`, `.summary`
   - Keep `planPictureZipEntryNames()` (entry names for compress output paths) 
   - Keep `collectFolderSummaryPictures()` (scan logic)
   - Keep `buildPicturePackEntryInputs()` / `buildCompressedPicturePackEntryInputs()` — they just build `PackEntryInput` vectors (which is a public type)
2. Verify `pack_api_standalone_compile_test.cpp` still compiles (picture includes only pack.h)
3. Run all tests: 945 assertions must pass with zero behavioral change

### Phase 4: PackPlan Pure Internalization (SINK-04)

**Rationale:** After Phase 3, only `pack.cpp` and `pack_service.cpp` use `PackPlan`. This phase makes it formally internal — verified by compile test.

**Changes:**
1. Verify no consumer outside `src/pack/` includes `pack_types.h` (PackPlan's home)
2. Update `pack_api_standalone_compile_test.cpp` to assert PackPlan is NOT visible from `pack.h`
3. Optionally: add `static_assert` that `pack::execute(PackPlan)` is only callable from within the pack module (compiler-level enforcement not practical without `friend` or modules, but compile test + code review suffices)

**This is a verification step more than a code change.** The heavy lifting was in Phase 3.

---

## Anti-Patterns

### Anti-Pattern 1: Keeping AppConfig Dependencies in NamingConfig

**What people do:** Copy `OutputLayout` and `forceConflictHandling` from `AppConfig` into `NamingConfig` and keep both alive. Two places to maintain the same logic.

**Why it's wrong:** Creates synchronization bugs when one is updated but not the other. Confuses which is the source of truth.

**Do this instead:** `NamingConfig` uses the new `NamingStrategy` enum (pack-local). Consumers translate `AppConfig` → `NamingStrategy` at the call site (one-line conversion). `AppConfig` keeps its fields for CLI parsing; they don't leak into pack.h.

### Anti-Pattern 2: Moving collision_naming.h into pack/

**What people do:** See collision_naming.h used by pack.cpp and move it to `src/pack/`.

**Why it's wrong:** `picture_process.cpp` uses these utilities for compress task keying (`buildCompressTaskKey` → `stablePathString`), which is a picture concern not a pack concern. Moving it creates a reverse dependency or forces picture to include a pack internal header.

**Do this instead:** Keep `collision_naming.h` in `src/core/`. It provides general-purpose inline path utilities used by multiple subsystems.

### Anti-Pattern 3: Adding PackEntryInput Builders to pack.h

**What people do:** Create factory functions or builder patterns for PackEntryInput inside pack.h to "help" consumers.

**Why it's wrong:** Adds complexity to the public API for a type that is simple (4-field aggregate with designated initializers). Consumers already know how to construct it.

**Do this instead:** Keep PackEntryInput as a simple aggregate in `pack_types.h` (included transitively by `pack.h`). Each consumer constructs it with designated initializers as needed.

---

## Risk Assessment

| Risk | Severity | Phase | Mitigation |
|------|----------|-------|------------|
| Picture entry name behavior drift | HIGH | Phase 3 | Preserve `planPictureZipEntryNames()` unchanged; it feeds compress output paths, not pack entry names. The naming strategy in pack.cpp handles pack entry names separately. |
| Logical grouping sort order change | MEDIUM | Phase 2 | The picture path sorts by `sourceKey` (stable-path-string of source dir). Must replicate this sort order exactly in `buildMediaPackPlan()`. Write targeted tests. |
| Resume state compatibility | MEDIUM | Phase 3 | Job state stores task labels based on zip names. If naming strategy changes zip name format, resume breaks. Mitigation: naming strategy does NOT change zip names — only zip entry names within archives. The zipNameStrategy callback format is unchanged. |
| NamingConfig backward compat | LOW | Phase 1 | Only 1 consumer uses NamingConfig (pipeline.cpp). Change is localized. |
| Compile-time regression | LOW | Phase 4 | `pack_api_standalone_compile_test.cpp` guards the public API boundary. Each phase must keep this test passing. |

---

## Sources

- **Source code (primary):** `src/pack/pack.h`, `src/pack/pack.cpp`, `src/pack/pack_types.h`, `src/pack/packer_types.h`, `src/pack/packer.h`, `src/pack/pack_service.h`, `src/pack/pack_internal.h`, `src/picture/picture_process.cpp`, `src/core/collision_naming.h`, `src/core/app_context.h` — all read and verified 2026-05-04
- **Project context:** `.planning/PROJECT.md` — v1.5 milestone definition, SINK-01 through SINK-04
- **Test patterns:** `tests/pack_execute_test.cpp`, `tests/pack_api_standalone_compile_test.cpp` — verified existing PackRequest construction patterns
- **Consumer usage:** `src/video/video_process.cpp:409-418`, `src/app/pipeline.cpp:52-66` — confirmed PackRequest construction patterns

---

*Architecture research for: encrō v1.5 — Naming strategy + grouping config + summary toggle integration*
*Researched: 2026-05-04*
