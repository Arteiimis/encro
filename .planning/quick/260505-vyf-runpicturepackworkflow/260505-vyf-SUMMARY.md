---
phase: quick
plan: 01
tags: [refactor, picture-process, readability, dispatcher-pattern]
requires: []
provides:
  - "Extracted helper functions: buildCompressedResultLookup, buildPicturePackRequest, buildPackEntryInputs, executeDirectPackWorkflow, executeCompressPackWorkflow"
  - "runPicturePackWorkflow routing dispatcher (9 lines)"
affects:
  - src/picture/picture_process.cpp
tech-stack:
  added: []
  patterns:
    - "Routing dispatcher pattern (~265→9 lines via anonymous namespace extraction)"
    - "Parameterized shared logic via callable source resolvers and transforms"
key-files:
  created: []
  modified:
    - src/picture/picture_process.cpp
decisions:
  - "All 5 helper functions placed in anonymous namespace — zero header changes, consistent with v1.1 lambda readability refactor pattern"
  - "buildPackEntryInputs uses parameterized SourceResolver callable + entryNameTransform to eliminate duplicate PackEntryInput loops"
metrics:
  duration: ~4min
  completed-date: 2026-05-05
  tasks: 2
  commits: 1
---

# Quick Task 260505-vyf: runPicturePackWorkflow routing dispatcher refactor — Summary

**One-liner:** Refactored `runPicturePackWorkflow` from a ~265-line monolith into a 9-line routing dispatcher by extracting 5 helper functions into the anonymous namespace, eliminating code duplication between compress and non-compress paths.

## Overview

`src/picture/picture_process.cpp` contained `runPicturePackWorkflow` (lines 197-463, ~265 lines) with heavily duplicated logic between the compress and non-compress code paths. Both paths performed nearly identical picture scanning, user confirmation, summary picture collection, zip entry name planning, PackEntryInput construction, PackRequest building, and pack execution — differing only in source resolution (compressed vs original) and entry name transformation (`.jpg` extension conversion vs identity).

This refactoring follows the established v1.1 "lambda readability refactor" pattern: extract to anonymous namespace, zero header modifications, zero external impact.

## What Changed

### New Functions (anonymous namespace)

| Function | Lines | Purpose |
|----------|-------|---------|
| `buildCompressedResultLookup` | 10 | Converts `CompressResult` vector → `(taskKey → compressedPath)` lookup map |
| `buildPicturePackRequest` | 18 | Unified `PackRequest` designated-initializer (parameterized by `baseName`) |
| `buildPackEntryInputs` | 55 | Parameterized `PackEntryInput` builder accepting `SourceResolver` + `entryNameTransform` callables |
| `executeDirectPackWorkflow` | 41 | Non-compress path orchestrator (identity source resolver, identity transform) |
| `executeCompressPackWorkflow` | 82 | Compress path orchestrator (compressed result resolver, `toJpgEntryName` transform) |

### Rewritten Function

**`runPicturePackWorkflow`** — 9 lines (was ~265), pure routing dispatcher:
```cpp
auto runPicturePackWorkflow(appctx::AppContext& ctx, fs::path const& dirPath)
  -> eh::Result<int> {
  auto const outputDir = ctx.config.outputPath.value_or(dirPath) / "packed";
  if (ctx.config.compressImages) {
    return executeCompressPackWorkflow(ctx, dirPath, outputDir);
  }
  return executeDirectPackWorkflow(ctx, dirPath, outputDir);
}
```

### What Was NOT Changed

- `picture_process.h` — zero modifications
- `packAllPicsToZip` (lines 492-583) — untouched
- All existing helpers (`readAllPics`, `planPictureZipEntryNames`, `collectFolderSummaryPictures`, `addCompressTask`, `confirmPicturePack`, etc.) — unchanged
- All terminal output messages — byte-for-byte identical to pre-refactor
- All error messages — byte-for-byte identical
- All control flow — identical behavior

## Verification

### Build
```
xmake build tests → build ok, spent 25.625s (zero errors, zero warnings)
```

### Tests
```
xmake run tests → All tests passed (3033 assertions in 244 test cases)
```

### Line Count
`runPicturePackWorkflow`: 9 lines (well within ≤15 line requirement)

### File Changes
```
git diff --name-only HEAD~1 → src/picture/picture_process.cpp (only file changed)
```

## Commits

| Hash | Message |
|------|---------|
| `2e7d78b` | `refactor(quick-260505-vyf): extract 5 helpers, route runPicturePackWorkflow to 8-line dispatcher` |

## Deviations from Plan

None — plan executed exactly as written.

## Known Stubs

None.

## Threat Flags

None.

## Self-Check

- [x] `src/picture/picture_process.cpp` — FOUND (583 lines, all 5 helpers + routing dispatcher present)
- [x] All 5 helper functions in anonymous namespace — VERIFIED
- [x] `runPicturePackWorkflow` ≤ 15 lines — VERIFIED (9 lines)
- [x] Zero header changes — VERIFIED (`git diff --name-only` shows only .cpp)
- [x] Commit `2e7d78b` exists — VERIFIED
- [x] All 3033 assertions pass — VERIFIED

## Self-Check: PASSED
