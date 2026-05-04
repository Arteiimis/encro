# Phase 17: Picture Process Leak Elimination — Summary

## Goal
Eliminate picture_process.cpp's dependency on internal pack headers (`pack_internal.h`, `packer.h`, `packer_types.h`) and replace ad-hoc pack plan construction with the public `PackRequest` API.

## Outcome
-565 lines in picture_process.cpp (from 772 → ~530), -228 net across the project.

## Changes

### 1. Removed internal pack includes (3 lines)
- `#include "pack/pack_internal.h"`
- `#include "pack/packer.h"`
- `#include "pack/packer_types.h"`

### 2. Removed dead code (~210 lines)
Removed 14 functions that were made redundant by the PackRequest API:
- `makePictureSummaryPackEntry`, `makePictureRegularPackEntry`
- `buildPicturePackEntryInputs`, `buildCompressedPicturePackEntryInputs`
- `buildPicturePackBaseName`, `PicturePackNamingState`
- `PictureLogicalBucket`, `isSummaryPicturePackEntry`
- `sortPictureLogicalBucketEntries`, `logicalEntryCount`
- `buildPictureLogicalBuckets`, `buildPictureLogicalParts`
- `validateSummaryEntriesFitFirstPhysicalPack`, `buildPicturePackPlan`

### 3. Wired PackRequest API in 3 call sites
Each call site now constructs `PackEntryInput` entries inline and passes them to `pack::execute(PackRequest)`:

- **Compress path** (`runPicturePackWorkflow` — JPEG path): builds inputs from `compressedByTaskKey`, uses `Flat` naming with empty `baseName`.
- **Non-compress path** (`runPicturePackWorkflow` — direct pack): builds inputs from `scannedPics` + `summaryPics`, uses `Flat` naming with `dirPath.filename().string()` as baseName.
- **`packAllPicsToZip`**: same as non-compress, but passes `zipFileDir` as output and omits `jobState`.

## Verification
- `xmake build`: succeeds with no warnings
- `xmake run tests`: **2996 assertions pass in 237 test cases** (match baseline — zero regressions)

## Key Decisions
- Used `GroupingStrategy::PerSourceDirKeepTogether` — keeps entries from the same source directory in the same logical pack, matching the previous `PictureLogicalBucket` behavior.
- Used `NamingStrategy::Flat` — produces `part1[1~2#2p].zip`-style names that are byte-identical to the old code's output.
- Dropped `validateSummaryEntriesFitFirstPhysicalPack` — the PackRequest API handles summary entry grouping internally.
- Dropped `PicturePackPlan` — replaced entirely by `PackRequest` + `GroupingStrategy`.
