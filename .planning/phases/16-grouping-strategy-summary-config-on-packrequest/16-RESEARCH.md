# Phase 16 Research: Grouping Strategy + Summary Config on PackRequest

**Researched:** 2026-05-04
**Status:** Complete

## Research Summary

Phase 16 extends `PackRequest` with two new capabilities: (1) declarative grouping strategy replacing
hardcoded threshold parameters, and (2) summary/cover-image entry injection with structural ordering
guarantees replacing fragile string-prefix conventions.

---

## 1. Current Grouping Dispatch

### 1.1 `buildMediaPackPlan()` (pack.cpp:106-250)

Current dispatch (line 174-178):
```cpp
auto const partitions = packer.groupPackEntriesWithSubparts(
    packInputs,
    kDefaultMaxArchiveGroupSize,   // 500MB size limit per physical sub-part
    kMaxEntriesPerPart,            // 2000 — sub-part count threshold
    kMaxEntriesPerPart             // 2000 — keepSourceDirsTogetherWhenTotalFilesExceed
);
```

Semantics: When total entries ≤ 2000, source dirs are kept together in the same sub-part.
When > 2000, entries are split strictly by size across sub-parts (dirs NOT kept together).

This is used by the **Media mode** (video pipeline, media pack).

### 1.2 Picture's `buildPicturePackPlan()` (picture_process.cpp:432-500)

Picture uses a two-layer partitioning:
1. **Logical buckets** (`buildPictureLogicalBuckets`): Group entries by source dir key
2. **Logical parts** (`buildPictureLogicalParts`): Pack buckets into parts ≤ kMaxPicturesPerPack entries
3. **Physical groups** (picture_process.cpp:460-465): For each logical part:
   ```cpp
   auto physicalGroups = packer.groupPackEntries(
       part, kDefaultMaxArchiveGroupSize, std::nullopt,
       std::optional<std::size_t>{0}  // keepSourceDirsTogetherWhenTotalFilesExceed=0
   );
   ```

Semantics: Source dirs are ALWAYS kept together (never split across archives), regardless of entry count.

### 1.3 Packer::groupPackEntriesWithSubparts() Parameters

From `packer.h:59-65`:
```cpp
auto groupPackEntriesWithSubparts(
    std::vector<pack::detail::PackEntryInput> const& entries,
    std::uintmax_t maxGroupSize,                                    // Physical size limit
    std::size_t maxFilesPerPart,                                    // Part count threshold
    std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed  // Strategy switch
) -> std::vector<pack::detail::PackEntryPartition>;
```

The `keepSourceDirsTogetherWhenTotalFilesExceed` parameter:
- **0** → Always keep source dirs together (picture behavior)
- **N** → Keep together when total entries ≤ N; split by size when > N (media behavior)
- **std::nullopt** → Standard size-only grouping (no keep-together semantics)

### 1.4 Design Decision: GroupingStrategy Enum

**Decision:** Map these behaviors to a single `GroupingStrategy` enum on `PackRequest`:

| Enum Value | keepSourceDirsTogetherWhenTotalFilesExceed | Use Case |
|-----------|------------------------------------------|----------|
| `PerSourceDir` | `kMaxEntriesPerPart` (2000) | Video pipeline, media pack |
| `PerSourceDirKeepTogether` | `0` (always) | Picture pack |

**Rationale:** Two values are sufficient — the "always keep together" vs "keep together up to threshold" distinction covers both current use cases. Adding a third value (like a configurable threshold) would be premature generalization without a use case.

**Phase 17 impact:** Phase 17 will route picture through `pack::execute(PackRequest)` instead of `buildPicturePackPlan()`. The `PerSourceDirKeepTogether` strategy in `buildMediaPackPlan()` must produce identical groupings to picture's current two-layer partitioning when picture provides the same `entryInputs`.

**Edge case:** `groupPackEntriesWithSubparts` does part-level splitting (by `maxFilesPerPart`) AND sub-part physical grouping (by `maxGroupSize`). Picture's `buildPictureLogicalParts()` controls the part-level splitting via `kMaxPicturesPerPack`. In `buildMediaPackPlan()` with `PerSourceDirKeepTogether`, the two-layer behavior merges into one call to `groupPackEntriesWithSubparts`. The `maxFilesPerPart` parameter becomes the analog of `kMaxPicturesPerPack`. This is verified via existing integration tests.

---

## 2. Current Summary Entry Handling

### 2.1 String Prefix Convention (picture_process.cpp)

Summary entries are currently identified by string prefix on `sourceKey`:

```cpp
// Summary entry (picture_process.cpp:168-169)
.sourceKey = std::format("0000__{}", dirKey),
.fileKey = std::format("0000__{}", dirKey),

// Regular entry (picture_process.cpp:186-187)  
.sourceKey = std::format("1000__{}", dirKey),
.fileKey = std::format("1000__{}", naming::stablePathString(picPath)),
```

Detection (picture_process.cpp:311-312):
```cpp
auto isSummaryPicturePackEntry(pack::detail::PackEntryInput const& input) -> bool {
    return input.sourceKey.has_value() && input.sourceKey->starts_with("0000__");
}
```

### 2.2 Summary Entry Naming

```cpp
// picture_process.cpp:35-42
auto buildSummaryPictureEntryName(fs::path const& dirPath, fs::path const& summaryPic) -> std::string {
    if (dirPath == summaryPic.parent_path()) {
        auto const entryName = summaryPic.filename().generic_string();
        return std::format("1000__{}", entryName);
    }
    return std::format("0000__summary__{}__{}", 
        summaryPic.parent_path().filename().string(),
        summaryPic.filename().string());
}
```

Summary entry zip names use `"0000__"` and `"1000__"` prefixes. These are hardcoded strings with no configuration.

### 2.3 Summary Ordering Guarantee

Currently, summary entries always appear first within each logical part (picture_process.cpp:397-398):
```cpp
if (bucket.summaryEntry.has_value()) {
    currentPart.push_back(bucket.summaryEntry.value());  // Summary first
}
currentPart.insert(currentPart.end(), bucket.regularEntries.begin(), bucket.regularEntries.end());
```

This relies on manual insertion ordering in `buildPictureLogicalParts()`. There is NO structural guarantee anywhere in the pack subsystem that summary entries come first — if `buildMediaPackPlan()` were used directly, summary entries would be interleaved with regular entries based on source dir sorting.

### 2.4 Design Decision: SummaryConfig + isSummary Flag

**Decision:** Replace string-prefix convention with:

1. **`bool isSummary` flag on `PackFileEntry`** (or `PackEntryInput`):
   - Set to `true` for summary/cover entries
   - Default `false` for regular entries

2. **`SummaryConfig` struct on `PackRequest`**:
   ```cpp
   struct SummaryConfig {
       std::vector<PackFileEntry> entries;  // Summary entries to inject
       std::string prefix = "";              // Configurable naming prefix (replaces "0000__")
       bool enabled = false;                 // Whether summary injection is active
   };
   ```

3. **Structural ordering guarantee in `buildMediaPackPlan()`**:
   - After grouping, sort each group so entries with `isSummary=true` come first
   - Use `std::stable_partition` or sort by `isSummary` descending
   - This replaces the manual insertion ordering in picture's `buildPictureLogicalParts()`

4. **Naming prefix**: `SummaryConfig::prefix` replaces hardcoded `"0000__"` and `"1000__"`. Default empty string (no prefix) preserves backward compatibility. Picture consumer sets prefix to match current behavior during migration.

### 2.5 Summary Deduplication

**Research gap from STATE.md**: "Summary deduplication behavior (when first picture = summary cover) needs decision."

Current behavior in `collectFolderSummaryPictures()` (picture_process.cpp:120-152):
- Groups pictures by source dir
- Sorts each dir's pictures alphabetically
- Picks `pictures.front()` (first alphabetically) as the summary
- The same picture also appears as a regular entry

**Decision:** Deduplication is NOT handled in Phase 16. The summary entry and its corresponding regular entry coexist in the archive — they represent the same file with potentially different zip entry names. This is existing behavior and not changed by Phase 16. Deduplication is a separate concern for a future phase.

---

## 3. Type Layout (What Goes Where)

### 3.1 New Types in `pack.h` (public API)

```cpp
// GroupingStrategy enum
enum class GroupingStrategy {
    PerSourceDir,            // Standard: keep dirs together up to threshold
    PerSourceDirKeepTogether // Always keep dirs together (picture behavior)
};

// SummaryConfig struct
struct SummaryConfig {
    std::vector<PackFileEntry> entries;  // Summary entry definitions
    std::string prefix = "";             // Naming prefix for summary entries
    bool enabled = false;                // Enable summary injection
};
```

### 3.2 Modification to `PackEntryInput` (pack_types.h)

```cpp
struct PackEntryInput {
    PackFileEntry entry;
    fs::path sourceDir;
    std::optional<std::string> sourceKey;
    std::optional<std::string> fileKey;
    bool isSummary = false;  // ← NEW: structural summary flag

    auto operator==(PackEntryInput const&) const -> bool = default;
};
```

### 3.3 Extension to `PackRequest` (pack.h)

```cpp
struct PackRequest {
    // ... existing fields ...
    GroupingStrategy groupingStrategy = GroupingStrategy::PerSourceDir;  // NEW
    std::optional<SummaryConfig> summary;  // NEW: nullopt = no summary injection
};
```

### 3.4 Why `isSummary` on `PackEntryInput` not `PackFileEntry`

`PackEntryInput` is the structured input type that carries semantic metadata (sourceDir, sourceKey, fileKey). The `isSummary` flag is semantic metadata about the entry's role, not a property of the file path or zip name. `PackFileEntry` remains a pure data transfer type (sourcePath + zipEntryName).

### 3.5 `PackFileEntry` also gets `isSummary` (for SummaryConfig)

`SummaryConfig.entries` uses `PackFileEntry` because summary entries may be specified as simple (path, name) pairs without the full `PackEntryInput` metadata. But summary entries from picture always come through `entryInputs` (which uses `PackEntryInput`).

**Decision:** Add `isSummary` to BOTH types:
- `PackFileEntry::isSummary` — for SummaryConfig.entries specification
- `PackEntryInput::isSummary` — for picture's entryInputs flow

In `buildMediaPackPlan()`, when converting `PackEntryInput` to `PackFileEntry` for grouping, propagate the flag.

---

## 4. Picture Consumer Migration Strategy

### 4.1 What Changes in Phase 16

Picture's `buildPicturePackPlan()`:
1. Set `isSummary = true` on summary entries instead of `sourceKey = "0000__..."`
2. Remove `isSummaryPicturePackEntry()` function (string-prefix-based detection)
3. Remove `"0000__"` / `"1000__"` prefix conventions from `sourceKey`/`fileKey`
4. Keep sourceKey as stable path string (for normal grouping) — no prefix needed
5. Use `isSummary` flag for ordering instead of manual insertion

### 4.2 What Does NOT Change in Phase 16

1. Picture still uses its own `buildPicturePackPlan()` — full PackRequest migration is Phase 17
2. Picture still does two-layer logical partitioning (buckets → parts) internally
3. Picture still calls `pack::execute(PackPlan)` — PackRequest entry point is Phase 17
4. `buildSummaryPictureEntryName()` naming still uses prefix format — but prefix is configurable via `SummaryConfig`

### 4.3 What Phase 17 Will Do

Phase 17 will eliminate `buildPicturePackPlan()` entirely and route picture through `pack::execute(PackRequest)` with `GroupingStrategy::PerSourceDirKeepTogether` and appropriate `SummaryConfig`.

---

## 5. Execution Path

### 5.1 `buildMediaPackPlan()` Modifications

Current flow:
1. Build packInputs from entries or entryInputs
2. Call `groupPackEntriesWithSubparts()` with hardcoded thresholds
3. Apply entryNameForFile callback
4. Build zip naming strategy
5. Return PackPlan

New flow (adds steps 1b and 2b):
1. Build packInputs from entries or entryInputs
1b. **Inject summary entries** from `SummaryConfig` if present (mark `isSummary=true`)
2. Call grouping function with strategy-determined parameters:
   - `PerSourceDir` → `keepTogetherWhenExceed = kMaxEntriesPerPart`
   - `PerSourceDirKeepTogether` → `keepTogetherWhenExceed = 0`
2b. **Ensure summary-first ordering** within each group via` isSummary` sort
3-5. Unchanged

### 5.2 Summary Injection Logic

Pseudo-code for `buildMediaPackPlan()` modification:
```cpp
// After building packInputs from entries/entryInputs...
if (request.summary.has_value() && request.summary->enabled) {
    for (auto const& summaryEntry : request.summary->entries) {
        packInputs.emplace_back(pack::detail::PackEntryInput{
            .entry = summaryEntry,
            .sourceDir = summaryEntry.sourcePath.parent_path(),
            .sourceKey = naming::stablePathString(summaryEntry.sourcePath.parent_path()),
            .fileKey = naming::stablePathString(summaryEntry.sourcePath),
            .isSummary = true  // ← structural flag
        });
    }
}
```

### 5.3 Summary-First Ordering

After grouping, before returning PackPlan:
```cpp
for (auto& group : groupedEntries) {
    std::ranges::stable_partition(group, [](PackFileEntry const& e) {
        return e.isSummary;
    });
}
```

This guarantees summary entries appear first in every archive group, replacing fragile string-prefix ordering.

---

## 6. Validation Architecture

### 6.1 Test Strategy

| Test Layer | What to Verify | File |
|-----------|---------------|------|
| Unit | `GroupingStrategy` enum values + dispatch in buildMediaPackPlan | `pack_execute_test.cpp` (extend) |
| Unit | `SummaryConfig` injection produces correct PackEntryInput entries with isSummary=true | `pack_execute_test.cpp` (extend) |
| Unit | Summary-first ordering via `stable_partition` | `pack_execute_test.cpp` (new test case) |
| Integration | Picture's buildPicturePackPlan produces identical groupings with isSummary flag | `picture_process_tests.cpp` |
| Integration | Existing packer grouping tests still pass | `packer_tests.cpp` |
| Integration | Existing pack service tests still pass | `pack_service_tests.cpp` |
| Integration | Existing pack execute tests still pass | `pack_execute_test.cpp` |
| Compile | Standalone compile test still passes with new types | `pack_api_standalone_compile_test.cpp` |

### 6.2 Nyquist Dimension

**Dimension 8 (Validation Coverage):** All existing integration tests serve as the verification baseline. New test cases added for:
- GroupingStrategy dispatch correctness
- isSummary flag propagation through grouping
- Summary-first ordering guarantee
- Removal of "0000__" prefix dependency in picture consumer

---

## 7. API Surface Changes Summary

| Component | File | Change |
|-----------|------|--------|
| `GroupingStrategy` enum | `pack.h` | NEW |
| `SummaryConfig` struct | `pack.h` | NEW |
| `PackRequest::groupingStrategy` | `pack.h` | NEW field |
| `PackRequest::summary` | `pack.h` | NEW field |
| `PackEntryInput::isSummary` | `pack_types.h` | NEW field |
| `PackFileEntry::isSummary` | `pack_types.h` | NEW field |
| `buildMediaPackPlan()` | `pack.cpp` | MODIFY: strategy dispatch + summary injection + ordering |
| `buildPicturePackPlan()` | `picture_process.cpp` | MODIFY: use isSummary flag, remove prefix convention |
| `isSummaryPicturePackEntry()` | `picture_process.cpp` | REMOVE |
| `makePictureSummaryPackEntry()` | `picture_process.cpp` | MODIFY: remove prefix strings |
| `makePictureRegularPackEntry()` | `picture_process.cpp` | MODIFY: remove prefix strings |

---

## 8. Risk Assessment

| Risk | Probability | Impact | Mitigation |
|------|-----------|--------|-----------|
| Grouping regression in video/media mode | Low | High | `PerSourceDir` maps to exact same parameters as current `kMaxEntriesPerPart`; existing tests verify |
| Picture grouping regression | Low | High | Picture's two-layer partitioning logic unchanged; only summary detection changes from prefix to flag |
| `isSummary` flag not propagated through grouping | Medium | Medium | Unit test verifies flag survives `groupPackEntriesWithSubparts()` |
| Empty SummaryConfig causes crash | Low | Low | `std::optional` guards; `enabled` flag provides explicit opt-in |
| Summary-first ordering fails for multi-group partitions | Medium | Medium | `stable_partition` per-group; unit test with multiple groups + summary entries |

---

## 9. Phase 15 Dependency Check

Phase 16 depends on Phase 15 (NamingStrategy enum + NamingConfig). Current codebase already has:
- ✅ `NamingStrategy` enum (`Flat`, `FlatWithForce`, `Keep`) in `pack.h`
- ✅ `NamingConfig` struct with `namingStrategy`, `baseName`, `zipNameStrategy` in `pack.h`
- ✅ `PackRequest::naming` field (std::optional<NamingConfig>)
- ✅ `buildMediaPackPlan()` resolves naming strategy with full dispatch (Keep common ancestor, conflict naming)

**No Phase 15 blocking issues.** Phase 16 builds on the existing naming infrastructure.

---

*Research complete: 2026-05-04*
