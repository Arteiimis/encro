# Phase 17: Picture Process Leak Elimination — Research

**Date:** 2026-05-04
**Status:** Complete
**Requirement:** SINK-03

## Domain: Picture → Pack Boundary Elimination

### Current State

`picture_process.cpp` (772 lines) includes 4 pack headers and constructs `PackPlan` directly via `buildPicturePackPlan()`. Phase 16 delivered `buildMediaPackPlan()` inside `pack.cpp` — the internal function that `pack::execute(PackRequest)` uses for Media mode — which now handles all the grouping, naming, and summary injection that `buildPicturePackPlan` previously did.

### Includes to Remove

| Line | Include | Type | Usage in file |
|------|---------|------|---------------|
| 9 | `pack/pack_internal.h` | INTERNAL | `pack::internal::appendOrdinalRangeSuffix`, `pack::internal::buildGroupOrdinalRanges` |
| 10 | `pack/packer.h` | INTERNAL | `pack::Packer` (line 452), `pack::detail::PackEntryInput` (via packer_types.h) |
| 11 | `pack/packer_types.h` | INTERNAL | `pack::detail::PackEntryInput` (extensively) |
| 8 | `pack/pack.h` | PUBLIC | KEEP — provides `pack::execute()`, `PackRequest`, `PackEntryInput`, etc. |

**Count: 3 internal pack includes to remove.** (`pack.h` stays as the public API entry point.)

### Internal Type Usage Audit

| Type | Current Usage | Replacement |
|------|--------------|-------------|
| `pack::detail::PackEntryInput` | Built in `makePictureSummaryPackEntry`, `makePictureRegularPackEntry` | Already aliased to `pack::PackEntryInput` in pack_types.h; change namespace to `pack::` |
| `pack::PackPlan` | Returned by `buildPicturePackPlan()` | Eliminated — `pack::execute(PackRequest)` internally constructs PackPlan |
| `pack::Packer` | `groupPackEntries()` call in `buildPicturePackPlan` | Eliminated — `buildMediaPackPlan` uses its own Packer internally |
| `pack::internal::appendOrdinalRangeSuffix` | In `PicturePackNamingState::zipNameFor` | Eliminated — handled by `makeDefaultZipNameStrategy` in pack.cpp |
| `pack::internal::buildGroupOrdinalRanges` | In `buildPicturePackPlan` | Eliminated — handled in `buildMediaPackPlan` |
| `pack::FileOrdinalRange` | In `PicturePackNamingState` | Available via `pack.h` (includes pack_types.h) |

### Functions to Remove / Replace

#### Removed functions (entirely):

| Function | Reason |
|----------|--------|
| `makePictureSummaryPackEntry()` | Replace with `pack::PackEntryInput` construction + `isSummary = true` |
| `makePictureRegularPackEntry()` | Replace with `pack::PackEntryInput` construction |
| `buildPicturePackEntryInputs()` | Replace with PackRequest.entryInputs population |
| `buildCompressedPicturePackEntryInputs()` | Replace with PackRequest.entryInputs population |
| `PictureLogicalBucket` struct | Eliminated — handled internally |
| `isSummaryPicturePackEntry()` | Check moved to entry construction time |
| `sortPictureLogicalBucketEntries()` | Eliminated — handled internally by groupPackEntriesWithSubparts |
| `logicalEntryCount()` | Eliminated |
| `buildPictureLogicalBuckets()` | Eliminated — PerSourceDirKeepTogether strategy handles this |
| `buildPictureLogicalParts()` | Eliminated — groupPackEntriesWithSubparts handles this |
| `validateSummaryEntriesFitFirstPhysicalPack()` | Eliminated — groupPackEntriesWithSubparts naturally handles size-based splitting |
| `buildPicturePackPlan()` | **Replaced by `pack::execute(PackRequest)`** |
| `PicturePackNamingState` struct | Eliminated — default naming in pack.cpp is byte-identical |
| `buildPicturePackBaseName()` | Eliminated — pack.cpp's `buildPackZipBaseName` is byte-identical |

#### Retained functions (picture-specific, no pack internal dependency):

| Function | Why Retained |
|----------|-------------|
| `buildFlatPictureEntryName()` | Picture-specific "1000__" prefix convention |
| `buildSummaryPictureEntryName()` | Picture-specific "0000__summary__" prefix convention |
| `shouldForcePictureConflictNaming()` | Translates AppConfig → NamingStrategy (consumer-side logic) |
| `buildConflictHandledPictureEntryName()` | Picture-specific collision naming wrapper |
| `toJpgEntryName()` | Extension conversion for compress path |
| `buildCompressTaskKey()` | Compress task dedup key |
| `planPictureZipEntryNames()` | Picture-specific entry name planning (calls retained functions) |
| `collectFolderSummaryPictures()` | Picture-specific summary collection |
| `addCompressTask()` | Compress task construction |
| `readAllPics()` | Media scanning |
| `confirmPicturePack()` | User confirmation |
| `runPicturePackWorkflow()` | Top-level orchestration (modified to use `pack::execute(PackRequest)`) |
| `packAllPicsToZip()` | API export (modified to use `pack::execute(PackRequest)`) |

### Architecture

#### Before (current):
```
picture_process.cpp
  ├─ planPictureZipEntryNames() → entry names
  ├─ buildPicturePackEntryInputs() → vector<PackEntryInput>
  ├─ buildPicturePackPlan()          ← constructs PackPlan directly
  │    ├─ buildPictureLogicalBuckets()
  │    ├─ buildPictureLogicalParts()
  │    ├─ validateSummaryEntriesFitFirstPhysicalPack()
  │    ├─ Packer::groupPackEntries()  ← internal type usage
  │    ├─ internal::buildGroupOrdinalRanges()
  │    └─ PicturePackNamingState (zipNameForIndex lambda)
  └─ pack::execute(PackPlan)          ← passes PackPlan
```

#### After (target):
```
picture_process.cpp
  ├─ planPictureZipEntryNames() → entry names
  ├─ Build vector<PackEntryInput> with zipEntryNames already set
  ├─ Build SummaryConfig with pre-built summary entries
  ├─ Construct PackRequest {
  │     .entryInputs = packInputs,
  │     .mode = Media,
  │     .outputDir = ...,
  │     .groupingStrategy = PerSourceDirKeepTogether,
  │     .summary = SummaryConfig{...},
  │     .naming = NamingConfig{.baseName = ...},
  │     .compact = ...,
  │     .maxParallelJobs = ...,
  │     .jobState = ...
  │   }
  └─ pack::execute(PackRequest)       ← single public API call
```

### Behavioral Equivalence Analysis

#### 1. Entry Naming (Zero Drift)

Picture entry names are built BEFORE passing to PackRequest. The retained functions (`buildFlatPictureEntryName`, `buildSummaryPictureEntryName`, `buildConflictHandledPictureEntryName`) produce the exact same strings as before. PackRequest.entryInputs carries these pre-built `zipEntryName` values, and `buildMediaPackPlan` uses them as-is when `entryInputs` is non-empty.

**Verification:** Golden zip entry name tests in `picture_process_tests.cpp` check exact string prefixes (`"0000__summary__"`, `"1000__"`). These must pass unchanged.

#### 2. Zip Archive Naming (Byte-Identical)

`buildPicturePackBaseName()` in picture_process.cpp:
```cpp
if (baseName.empty()) {
    if (totalSubParts <= 1) return format("part{}.zip", partIndex);
    return format("part{}.{}.zip", partIndex, subPartIndex + 1);
}
if (totalSubParts <= 1) return format("{}_part{}.zip", baseName, partIndex);
return format("{}_part{}.{}.zip", baseName, partIndex, subPartIndex + 1);
```

`buildPackZipBaseName()` in pack.cpp:
```cpp
if (!baseName.empty()) zipBase = format("{}_part{}", baseName, partIndex);
else zipBase = format("part{}", partIndex);
if (totalSubParts > 1) zipBase += format(".{}.zip", subPartIndex + 1);
else zipBase += ".zip";
```

**These produce byte-identical output** for all inputs. The `appendOrdinalRangeSuffix` call chain is identical in both (same `pack::internal::` function).

#### 3. Grouping Strategy Equivalence

Picture's manual two-layer partitioning:
- Buckets: group by `stablePathString(sourceDir)` — same as `PerSourceDirKeepTogether`
- Parts: split at 2000 entries per logical part, keeping buckets together — same as `groupPackEntriesWithSubparts(keepTogetherThreshold=0, maxFilesPerPart=2000)`
- Physical: `groupPackEntries(keepSourceDirsTogetherWhenTotalFilesExceed=0)` — same as `groupPackEntriesWithSubparts(keepTogetherThreshold=0)`

`buildMediaPackPlan` uses `groupPackEntriesWithSubparts` with `PerSourceDirKeepTogether` → `keepTogetherThreshold = 0`, which matches exactly.

#### 4. Summary Ordering

Both picture_process and `buildMediaPackPlan` use `std::ranges::stable_partition` with `isSummary` predicate to ensure summary entries appear first in each group. **Behaviorally identical.**

#### 5. Summary Entry Validation (`validateSummaryEntriesFitFirstPhysicalPack`)

Picture_process validates that all summary entries in a logical part fit within `kDefaultMaxArchiveGroupSize` (500MB). This is a pre-check that warns early if summary images are too large.

`buildMediaPackPlan` does NOT have this explicit check — it relies on `groupPackEntriesWithSubparts` to naturally split groups by size. While the early error is lost, the actual behavior is safe: if summaries exceed 500MB, they'll be split across multiple physical archives with the first part containing what fits and the rest in subsequent parts.

**Decision:** Drop this pre-check. The rare case of summary images exceeding 500MB is handled gracefully by the physical grouping, and the error message was informational rather than correctness-critical. If needed, picture_process can add its own size check before constructing PackRequest (without pack internal dependencies).

### Compress Path Integration

The compress path builds `PackEntryInput` objects with compressed `.jpg` paths as `sourcePath` and `.jpg` entry names. This is fully compatible with `pack::execute(PackRequest)` — the `entryInputs` field carries pre-built entries with the correct source paths and zip names.

### Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Behavioral drift in zip entry names | Low | High | Golden tests verify byte-identical output; entry name functions retained unchanged |
| Grouping difference (PerSourceDirKeepTogether vs manual) | Low | Medium | Same underlying Packer functions; tests verify partition counts and summary placement |
| Summary entry validation gap | Low | Low | Acceptable; physical grouping handles oversized summaries gracefully |
| Compress path entry ordering difference | Low | Medium | stable_partition in both paths; tests verify summary-first ordering |
| Resumable job state compatibility | Low | High | Same `jobstate::Store` pointer passed through; `execute(PackRequest)` → `execute(PackPlan, jobState)` internally |

### Dependencies

- **Phase 15 (complete):** NamingStrategy enum, NamingConfig with baseName
- **Phase 16 (complete):** GroupingStrategy enum, SummaryConfig, isSummary flag, buildMediaPackPlan
- **Phase 18 (blocked on):** PackPlan internalization (Phase 17 must complete first)

### Discovery Level

**Level 0** — pure internal refactoring. No new libraries, no new external dependencies. All patterns are established in the codebase (Phase 16's `buildMediaPackPlan` is the reference implementation). The work is entirely about removing internal pack type usage from picture_process.cpp and routing through the already-existing `pack::execute(PackRequest)` API.

### Implementation Strategy

1. **Replace `buildPicturePackPlan`** with PackRequest construction + `pack::execute(PackRequest)` in all 3 call sites (non-compress, compress, packAllPicsToZip)
2. **Remove internal includes** (packer.h, packer_types.h, pack_internal.h)
3. **Remove dead code** (all eliminated functions/structs)
4. **Change `pack::detail::PackEntryInput`** → `pack::PackEntryInput` (same type, public namespace)
5. **Verify:** Run full test suite, confirm all picture tests pass with byte-identical output

### Files Modified

| File | Change |
|------|--------|
| `src/picture/picture_process.cpp` | Remove 3 internal includes, replace buildPicturePackPlan with PackRequest, remove dead code, rename pack::detail:: → pack:: |
| `src/picture/picture_process.h` | No changes (public API unchanged) |
| `tests/picture/picture_process_tests.cpp` | Verify all tests pass unchanged |

## Validation Architecture

- **Dimension 1 (Correctness):** All 520+ lines of picture process tests pass with zero modifications
- **Dimension 2 (Completeness):** Compile check: `grep -c "packer.h\|pack_internal.h\|packer_types.h" src/picture/picture_process.cpp` returns 0
- **Dimension 3 (Non-regression):** Full test suite (945+ assertions) passes
- **Dimension 4 (Golden):** Zip entry name patterns ("0000__summary__", "1000__") verified in test assertions

### Estimated Scope

- ~200 lines removed from picture_process.cpp
- ~80 lines added (PackRequest construction)
- ~120 lines net reduction
- 1 file modified, 0 new files
- ~15-20% context budget for implementation
