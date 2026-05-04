---
phase: 17
slug: picture-process-leak-elimination
status: verified
verified_at: 2026-05-05
requirements_covered: [SINK-03]
---

# Phase 17 — Goal Verification

> Verifies that Phase 17 delivered what it promised: picture_process.cpp no longer includes internal pack headers; constructs PackRequest instead of building PackPlan directly.

## Goal-Requirement Mapping

| Goal | REQ | Status |
|------|:---:|:------:|
| Remove all internal pack includes from picture_process.cpp (pack_internal.h, packer.h, packer_types.h) | SINK-03 | verified |
| Replace ad-hoc PackPlan construction with PackRequest API | SINK-03 | verified |
| Golden zip entry name tests pass with byte-identical output | SINK-03 | verified |

## Artifact Evidence

### Code
- `src/picture/picture_process.cpp`: -565 lines (772→~530), -3 internal includes, -14 dead functions
- Removed functions: `makePictureSummaryPackEntry`, `makePictureRegularPackEntry`, `buildPicturePackEntryInputs`, `buildCompressedPicturePackEntryInputs`, `buildPicturePackBaseName`, `PicturePackNamingState`, `PictureLogicalBucket`, `isSummaryPicturePackEntry`, `sortPictureLogicalBucketEntries`, `logicalEntryCount`, `buildPictureLogicalBuckets`, `buildPictureLogicalParts`, `validateSummaryEntriesFitFirstPhysicalPack`, `buildPicturePackPlan`
- 3 call sites now construct `PackEntryInput` entries inline → `pack::execute(PackRequest)`:
  - Compress path (JPEG): `Flat` naming, empty `baseName`
  - Non-compress path: `Flat` naming, `dirPath.filename().string()` as baseName
  - `packAllPicsToZip`: same as non-compress, omits `jobState`
- Uses `GroupingStrategy::PerSourceDirKeepTogether` — matches previous `PictureLogicalBucket` behavior
- Uses `NamingStrategy::Flat` — byte-identical zip entry names

### Tests
- Golden zip entry name tests pass with byte-identical output
- Full suite: 3033 assertions in 244 test cases — zero failures (matches baseline)

### Documentation
- `17-01-SUMMARY.md`: Full account of removed code, new API wiring, key decisions
- `17-VALIDATION.md`: Nyquist-compliant validation strategy (per-task verification map)
- `17-01-PLAN.md`: Execution plan

## Leak Elimination Verification

| Internal Dep | Before | After |
|:---|:---|:---|
| `#include "pack/pack_internal.h"` | present | removed |
| `#include "pack/packer.h"` | present | removed |
| `#include "pack/packer_types.h"` | present | removed |
| Direct `PackPlan` construction | yes (14 functions) | no (PackRequest API) |
| `PackFileEntry` / `PackEntryInput` | consumer constructing | consumer constructing (public types, allowed) |

## Verdict

**VERIFIED** — All internal pack dependencies removed from picture_process.cpp. The picture consumer now exercises only the public PackRequest API. No behavioral regression: 3033 assertions pass, byte-identical output. REQUIREMENTS.md SINK-03 [x] and SUMMARY frontmatter populated.
