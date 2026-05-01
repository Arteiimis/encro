# 13-03 SUMMARY: Picture Consumer Simplification

**Phase:** 13-grouping-naming | **Plan:** 03 (Wave 3)
**Depends on:** 13-02
**Completed:** 2026-05-01
**Status:** All tasks complete. All tests pass.

## Tasks Completed

### Task 1: Simplify non-compress picture path — remove buildPicturePackPlan + related types

#### Non-compress path (runPicturePackWorkflow) simplified:
- Directly calls `pack::execute()` with full `PackRequest` including:
  - `NamingConfig{.baseName = dirPath.filename().string(), .layout = ..., .forceConflictHandling = ...}`
  - `entryNameForFile` callback built from `planPictureZipEntryNames` result
  - Proper field ordering (`jobState` before `entryNameForFile`)

#### packAllPicsToZip simplified:
- Same pattern: direct `pack::execute()` call with `NamingConfig` + `entryNameForFile`

#### Deleted obsolete types and functions:
- `PicturePackNamingState` struct — internalized to pack.cpp (as `NamingState` in `makeDefaultZipNameStrategy`)
- `buildPicturePackBaseName` function — internalized to pack.cpp (as `buildPackZipBaseName`)
- `PreparedPicturePack` struct
- `preparePicturePack` function
- `printPicturePackWorkflowSummary` function
- `buildPicturePackPlan` (both overloads) — fully removed from .h and .cpp

#### Include cleanup:
- Removed `#include "pack/pack_service.h"` — no longer needed
- Removed `#include "pack/pack_internal.h"` — no longer needed
- Removed `#include "pack/packer.h"` — no longer needed
- Removed `#include <memory>` — no longer needed
- Kept `#include "pack/packer_types.h"` — needed for `pack::detail::PackEntryInput`
- Kept `#include "pack/pack.h"` — needed for `pack::execute()` + `PackRequest` + `NamingConfig`

#### Header simplification (picture_process.h):
- Removed `#include "pack/pack_types.h"` — no longer needed
- Removed `#include <span>` — no longer needed
- Removed both `buildPicturePackPlan` declarations

## Verification
- `xmake build encro` exits 0 (no warnings when field order is correct)
- Zero references to `buildPicturePackPlan` in src/ or tests/
- Zero references to `PicturePackNamingState`, `buildPicturePackBaseName`, `PreparedPicturePack`, `preparePicturePack`, `printPicturePackWorkflowSummary` in src/
- `pack::execute` called from picture_process.cpp in 3 places (non-compress path, compress path, packAllPicsToZip)
- No `#include "pack/pack_service.h"` or `#include "pack/packer.h"` in picture_process.cpp

## Files Modified
- `src/picture/picture_process.cpp` — simplified (592→~410 lines), obsolete types deleted, includes cleaned
- `src/picture/picture_process.h` — buildPicturePackPlan declarations removed, includes trimmed
