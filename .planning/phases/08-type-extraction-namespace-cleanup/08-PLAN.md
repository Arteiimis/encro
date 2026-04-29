---
phase: 8
plan: 08-type-extraction-namespace-cleanup
type: execute
wave: 1
depends_on: []
files_modified:
  - src/pack/pack_types.h (NEW)
  - src/pack/packer_types.h (NEW)
  - src/pack/packer.h (MODIFIED)
  - src/pack/pack_service.h (MODIFIED)
  - src/pack/packer.cpp (MODIFIED)
  - src/video/video_output_planning.cpp (MODIFIED)
  - src/picture/picture_process.cpp (MODIFIED)
  - src/app/pipeline.cpp (MODIFIED)
  - tests/packer_tests.cpp (MODIFIED)
  - tests/packer_standalone_compile_test.cpp (NEW)
autonomous: true
requirements:
  - TYPE-01
  - TYPE-02
  - TYPE-03
  - TYPE-04
---

<objective>
Extract shared value types to `pack_types.h`, move global-scope structs to `pack::detail::` in `packer_types.h`, break the `packer.h` ↔ `pack_service.h` circular dependency. Zero behavioral change — pure header/include refactoring. All 909 assertions must pass.
</objective>

<tasks>

<task id="T01" type="execute" depends_on="">

<read_first>
- src/pack/pack_service.h (current PackFileEntry, FileOrdinalRange, PackRunResult definitions at lines 19-35)
- src/pack/packer.h (current `kDefaultMaxArchiveGroupSize` definition at lines 18-21)
- Existing header conventions: `#pragma once`, `namespace fs = std::filesystem;`, grouped includes
</read_first>

<action>
Create NEW FILE `src/pack/pack_types.h` with the following content:

```cpp
#pragma once

#include "core/error_handle.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace pack {

struct PackRunResult {
  int exitCode = 0;
  std::vector<fs::path> zippedFiles;
};

struct PackFileEntry {
  fs::path sourcePath;
  std::string zipEntryName;

  auto operator==(PackFileEntry const&) const -> bool = default;
};

struct FileOrdinalRange {
  std::size_t first = 0;
  std::size_t last = 0;
  std::size_t count = 0;
};

inline constexpr auto kDefaultMaxArchiveGroupSize = std::uintmax_t{500 * 1024 * 1024};

}  // namespace pack
```

Notes:
- `PackRunResult`, `PackFileEntry`, `FileOrdinalRange` extracted verbatim from `pack_service.h` lines 19-35 — zero field, type, or default changes
- `kDefaultMaxArchiveGroupSize` extracted from `packer.h` lines 18-21 (D-04: placed alongside value types that consume it)
- `#include "core/error_handle.h"` for `eh::Result<T>` — consumers including `pack_types.h` often also need error handling types
</action>

<acceptance_criteria>
- `src/pack/pack_types.h` exists with `#pragma once` as first non-comment line
- Contains `namespace pack { ... }` wrapping all 3 structs + 1 constant
- `PackRunResult` has: `int exitCode = 0`, `std::vector<fs::path> zippedFiles` (same types and defaults)
- `PackFileEntry` has: `fs::path sourcePath`, `std::string zipEntryName`, `operator== default`
- `FileOrdinalRange` has: `std::size_t first = 0`, `std::size_t last = 0`, `std::size_t count = 0`
- `kDefaultMaxArchiveGroupSize` is `inline constexpr auto` with value `std::uintmax_t{500 * 1024 * 1024}`
- `#include "core/error_handle.h"` present, `#include <cstdint>` present
- `grep -c "PackRunResult" src/pack/pack_types.h` → 1 (single definition)
- `grep -c "PackFileEntry" src/pack/pack_types.h` → 1
- `grep -c "FileOrdinalRange" src/pack/pack_types.h` → 1
</acceptance_criteria>

</task>

<task id="T02" type="execute" depends_on="">

<read_first>
- src/pack/packer.h (current PackGroupInput, PackGroupPartition, PackEntryInput, PackEntryPartition structs at lines 24-46)
- Existing precedent: `videobatch::detail` namespace pattern from `src/video/video_batch_execution.h` (v1.2)
</read_first>

<action>
Create NEW FILE `src/pack/packer_types.h` with the following content:

```cpp
#pragma once

#include "pack/pack_types.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pack::detail {

struct PackGroupInput {
  std::filesystem::path filePath;
  std::filesystem::path sourceDir;
};

struct PackGroupPartition {
  std::vector<std::filesystem::path> filePaths;
  std::size_t partIndex = 0;
  std::size_t subPartIndex = 0;
};

struct PackEntryInput {
  pack::PackFileEntry entry;
  std::filesystem::path sourceDir;
  std::optional<std::string> sourceKey;
  std::optional<std::string> fileKey;
};

struct PackEntryPartition {
  std::vector<pack::PackFileEntry> entries;
  std::size_t partIndex = 0;
  std::size_t subPartIndex = 0;
};

}  // namespace pack::detail
```

Notes:
- All 4 structs extracted verbatim from `packer.h` lines 24-46 — zero field, type, or default changes
- Namespace changed from global to `pack::detail::` (D-01, TYPE-02)
- `PackEntryInput` and `PackEntryPartition` use `pack::PackFileEntry` — hence `#include "pack/pack_types.h"`
- `namespace pack::detail { ... }` — nested namespace syntax, clean and idiomatic
</action>

<acceptance_criteria>
- `src/pack/packer_types.h` exists with `#pragma once` as first non-comment line
- `#include "pack/pack_types.h"` present
- `namespace pack::detail { ... }` wraps all 4 structs
- `PackGroupInput`: `filePath` (fs::path), `sourceDir` (fs::path) — same types
- `PackGroupPartition`: `filePaths` (vector<fs::path>), `partIndex = 0`, `subPartIndex = 0`
- `PackEntryInput`: `entry` (pack::PackFileEntry), `sourceDir` (fs::path), `sourceKey` (optional<string>), `fileKey` (optional<string>)
- `PackEntryPartition`: `entries` (vector<pack::PackFileEntry>), `partIndex = 0`, `subPartIndex = 0`
- No global-scope type definitions in this header
- `grep "using namespace\|^using" src/pack/packer_types.h` → 0 matches (no using-declarations leaking)
</acceptance_criteria>

</task>

<task id="T03" type="execute" depends_on="T01,T02">

<read_first>
- src/pack/packer.h (current full file — 126 lines)
- src/pack/pack_types.h (new file from T01)
- src/pack/packer_types.h (new file from T02)
- CONTEXT.md: D-01 (aliases → pack::detail::), D-04 (kDefaultMaxArchiveGroupSize → pack_types.h)
</read_first>

<action>
Modify `src/pack/packer.h` with the following changes (in order):

### 1. Replace include block (line 5)
**REMOVE:** `#include "pack/pack_service.h"`
**ADD** (in its place):
```cpp
#include "core/app_context.h"
#include "pack/pack_types.h"
#include "pack/packer_types.h"
```

Rationale: `core/app_context.h` is needed for `appctx::AppContext&` in function signatures (lines 114, 126). It does NOT depend on any pack headers — safe to include. `pack_types.h` provides PackFileEntry, PackRunResult, FileOrdinalRange, and kDefaultMaxArchiveGroupSize. `packer_types.h` provides pack::detail:: types.

### 2. Move aliases to pack::detail:: (lines 15-16)
**REMOVE** lines 15-16:
```cpp
using ZipEntryNameResolver = std::function<std::string(std::filesystem::path const&)>;
using PackEntryProgressCallback = std::function<void(std::size_t, std::size_t)>;
```
**ADD** after the includes (before the existing `namespace pack {` ... wait, actually there is no `namespace pack {` left after removing the constant block):

Place after the last `#include` and before the first function declaration:
```cpp
namespace pack::detail {

using ZipEntryNameResolver = std::function<std::string(std::filesystem::path const&)>;
using PackEntryProgressCallback = std::function<void(std::size_t, std::size_t)>;

}  // namespace pack::detail
```

### 3. Remove constant block (lines 18-21)
**REMOVE**:
```cpp
namespace pack {
inline constexpr auto kDefaultMaxArchiveGroupSize = std::uintmax_t{500 * 1024 * 1024};
}
```
(Moved to `pack_types.h` — D-04)

### 4. Remove global-scope structs (lines 24-46)
**REMOVE** all 4 struct definitions: `PackGroupInput`, `PackGroupPartition`, `PackEntryInput`, `PackEntryPartition`
(Moved to `packer_types.h` in `pack::detail::`)

### 5. Update type references in function signatures
All function signatures that currently use unqualified type names must be updated:

| Current (line) | Replace With |
|---|---|
| `ZipEntryNameResolver` (line 53) | `pack::detail::ZipEntryNameResolver` |
| `PackEntryProgressCallback` (line 66) | `pack::detail::PackEntryProgressCallback` |
| `PackGroupInput` (lines 77, 84) | `pack::detail::PackGroupInput` |
| `PackGroupPartition` (line 89) | `pack::detail::PackGroupPartition` |
| `PackEntryInput` (lines 91, 98) | `pack::detail::PackEntryInput` |
| `PackEntryPartition` (line 102) | `pack::detail::PackEntryPartition` |
| `pack::kDefaultMaxArchiveGroupSize` (lines 104, 121) | `kDefaultMaxArchiveGroupSize` (now in pack:: via pack_types.h) |

### 6. Final header structure
After all changes, `packer.h` should have:
- Includes: `core/error_handle.h`, `core/progress.h`, `core/app_context.h`, `pack/pack_types.h`, `pack/packer_types.h`, standard headers
- `pack::detail::ZipEntryNameResolver` and `pack::detail::PackEntryProgressCallback` aliases
- All free function declarations with fully-qualified type names
- Zero global-scope definitions
- Zero `#include "pack/pack_service.h"` — circular dependency broken
</action>

<acceptance_criteria>
- `grep '#include "pack/pack_service.h"' src/pack/packer.h` → empty (circular dependency broken)
- `grep '#include "pack/pack_types.h"' src/pack/packer.h` → 1 match
- `grep '#include "pack/packer_types.h"' src/pack/packer.h` → 1 match
- `grep '#include "core/app_context.h"' src/pack/packer.h` → 1 match (needed for AppContext& in signatures)
- `grep "using ZipEntryNameResolver" src/pack/packer.h` → empty (global alias removed)
- `grep "using PackEntryProgressCallback" src/pack/packer.h` → empty (global alias removed)
- `grep "pack::detail::ZipEntryNameResolver" src/pack/packer.h` → ≥1 match
- `grep "pack::detail::PackEntryProgressCallback" src/pack/packer.h` → ≥1 match
- `grep "inline constexpr.*kDefaultMaxArchiveGroupSize" src/pack/packer.h` → empty (moved)
- `grep "struct PackGroupInput\b" src/pack/packer.h` → empty (moved to packer_types.h)
- `grep "struct PackGroupPartition\b" src/pack/packer.h` → empty
- `grep "struct PackEntryInput\b" src/pack/packer.h` → empty
- `grep "struct PackEntryPartition\b" src/pack/packer.h` → empty
- `grep "pack::detail::PackGroupInput" src/pack/packer.h` → ≥1 match
- `grep "pack::detail::PackEntryInput" src/pack/packer.h` → ≥1 match
- `grep "^\s*(using|struct)\s" src/pack/packer.h` → empty (no un-namespaced declarations at namespace scope)
- `<cstdint>` still present (needed for `std::uintmax_t` in groupFilesBySize, packAllFilesInDirectory, buildDirectoryPackPlan signatures)
</acceptance_criteria>

</task>

<task id="T04" type="execute" depends_on="T01">

<read_first>
- src/pack/pack_service.h (current full file — 82 lines)
- src/pack/pack_types.h (new file from T01)
- CONTEXT.md: D-02 (pack_service.h MUST #include pack_types.h)
</read_first>

<action>
Modify `src/pack/pack_service.h`:

### 1. Add include (after line 4: `#include "core/error_handle.h"`)
**ADD:**
```cpp
#include "pack/pack_types.h"
```

### 2. Remove extracted struct definitions (lines 19-35)
**REMOVE** the three struct definitions:
- `PackRunResult` (lines 19-22)
- `PackFileEntry` (lines 24-29)
- `FileOrdinalRange` (lines 31-35)

These are now provided by `pack_types.h` (included above).

### 3. Keep everything else unchanged
- PackPlan struct (lines 37-50) — stays (not part of Phase 8)
- `static_assert(std::is_aggregate_v<pack::PackPlan>)` (lines 52-55) — stays
- All free function declarations — stay unchanged
- All includes — stay except duplicates covered by pack_types.h
  - `#include <string>` (line 11): was needed for PackFileEntry.zipEntryName. Now provided via pack_types.h. Keep it — PackPlan uses `std::string` in callback return types.
  - `#include <type_traits>` (line 12): needed for `std::is_aggregate_v` in static_assert. KEEP.
  - `#include <filesystem>` (line 6): needed for `fs::path` in PackPlan. KEEP.
  - `#include <vector>` (line 13): needed for `std::vector` in PackPlan. KEEP.

Final include block:
```cpp
#include "core/app_context.h"
#include "core/error_handle.h"
#include "pack/pack_types.h"
// ... standard headers unchanged
```
</action>

<acceptance_criteria>
- `grep '#include "pack/pack_types.h"' src/pack/pack_service.h` → 1 match (D-02 compliant)
- `grep "struct PackRunResult" src/pack/pack_service.h` → empty (extracted to pack_types.h)
- `grep "struct PackFileEntry" src/pack/pack_service.h` → empty
- `grep "struct FileOrdinalRange" src/pack/pack_service.h` → empty
- `grep "struct PackPlan" src/pack/pack_service.h` → 1 match (PackPlan untouched)
- `grep "static_assert.*is_aggregate_v.*PackPlan" src/pack/pack_service.h` → 1 match (guard preserved)
- `#include <type_traits>` present (needed for `std::is_aggregate_v`)
- All 9 free function declarations (`buildGroupOrdinalRanges` × 2, `appendOrdinalRangeSuffix`, `defaultZipNameForIndex`, `defaultProgressLabelForZipName`, `resolveZipNameForIndex`, `resolveProgressLabelForIndex`, `selectPackPlanIndexes`, `runPackPlan`, `packGroups`) present with identical signatures
- `grep -c "PackRunResult" src/pack/pack_service.h` → 1 (only in return type `eh::Result<PackRunResult>` — struct definition is gone)
- `grep -c "PackFileEntry" src/pack/pack_service.h` → ≥2 (used in PackPlan.groups type + function parameters — struct definition is gone)
</acceptance_criteria>

</task>

<task id="T05" type="execute" depends_on="T03,T04">

<read_first>
- src/pack/packer.cpp (includes at lines 1-20; uses PackGroupInput, PackEntryInput, etc. in anonymous namespace)
- src/video/video_output_planning.cpp (include at line 4 `pack/packer.h`; uses PackGroupInput, PackEntryInput in anonymous namespace)
- src/picture/picture_process.cpp (includes at lines 8-9; uses PackEntryInput)
- src/app/pipeline.cpp (includes at lines 1-9; only includes `pack/packer.h` — after T03, may lose transitive `pack_service.h`)
- tests/packer_tests.cpp (include at line 1; uses PackGroupInput)
</read_first>

<action>
Update consumer `#include` paths and namespace references. Zero logic changes — only `#include` and `using` adjustments.

### File-by-file changes:

#### 1. `src/pack/packer.cpp`
- **ADD** after the last `#include` (line 20), before the unnamed namespace:
  ```cpp
  using namespace pack::detail;
  ```
- **NO include changes needed**: packer.h (line 1) now provides pack_types.h + packer_types.h; pack_service.h (line 7) provides pack::runPackPlan + pack::packGroups.
- Usage: `PackGroupInput`, `PackEntryInput`, `PackGroupPartition`, `PackEntryPartition` used extensively (29 sites) in anonymous namespace — the `using namespace` resolves all.

#### 2. `src/video/video_output_planning.cpp`
- **ADD** after the last `#include` (line 5), before line 7:
  ```cpp
  using namespace pack::detail;
  ```
- **NO include changes needed**: already includes `pack/packer.h` which now provides pack_types.h + packer_types.h.
- Usage: `PackGroupInput` and `PackEntryInput` at lines 177, 180, 190, 194.

#### 3. `src/picture/picture_process.cpp`
- **ADD** after the last `#include` (line 12), before line 14:
  ```cpp
  using namespace pack::detail;
  ```
- **NO include changes needed**: includes `pack/pack_service.h` (line 8) and `pack/packer.h` (line 9) — both include pack_types.h now. packer.h provides packer_types.h.
- Usage: `PackEntryInput` at lines 146, 151, 157, 163, 184, 390, 402, 427, 576.

#### 4. `src/app/pipeline.cpp`
- **ADD** `#include "pack/pack_service.h"` after line 5 (`#include "pack/packer.h"`).
- Rationale: pipeline.cpp uses `pack::PackPlan` (in `buildDirectoryPackPlan` return type accessed through callers) AND `pack::runDirectoryPackWorkflow` which internally uses PackPlan. Previously PackPlan was transitively available through `packer.h` → `pack_service.h`. After T03, packer.h no longer includes pack_service.h, so pipeline.cpp needs its own include.
- Actually, checking pipeline.cpp more carefully: it only calls `runDirectoryPackWorkflow(ctx, ctx.config.inputPath)` at line 51 (return type `eh::Result<int>` — no PackPlan needed) and `runPackPlan` is NOT called directly. `appctx::AppContext` is available through `video/video_process.h` (line 8) which includes `pack/pack_service.h` → `core/app_context.h`.
- **VERDICT: NO CHANGE needed for pipeline.cpp** — it doesn't directly reference `pack::PackPlan` or `pack::` free functions from pack_service.h. It gets AppContext transitively through video_process.h or picture_process.h.

Wait — let me re-verify. pipeline.cpp includes:
- `core/job_state.h` (line 3)
- `pack/packer.h` (line 5) — after T03: provides pack_types.h, packer_types.h, and forward declaration of pack::PackPlan (via packer.h)
- `picture/picture_process.h` (line 6) — includes `pack/pack_service.h` → provides full PackPlan
- `video/video_process.h` (line 8) — includes `pack/pack_service.h` transitively → provides full PackPlan
- `utils/utils.h` (line 7)

So pipeline.cpp gets PackPlan AND AppContext through its other includes. **NO CHANGE needed.** But to be safe and explicit, I'll note that pipeline.cpp is fine without changes because its other includes provide the transitive types.

Actually, I realize I should double-check: `video/video_process.h` — does it include pack_service.h? Let me check.

Looking at `src/video/video_process.cpp` line 11: `#include "pack/pack_service.h"`. But the header `video/video_process.h` might be different.

Let me check what `video/video_process.h` includes by looking at the .cpp file's first line: `#include "video/video_process.h"`. The header is at `src/video/video_process.h`.

Hmm, I should verify this. But I already know from the CONTEXT.md: "src/core/archive_plan.h — includes pack/pack_service.h". And from the file list, picture_process.h includes pack_service.h (line 5: `#include "pack/pack_service.h"`).

For pipeline.cpp, the safest approach: just leave it unchanged. If build fails because PackPlan is incomplete, the fix is trivial (add `#include "pack/pack_service.h"`). The build will tell us.

Let me be more precise about the pipeline:

pipeline.cpp includes `picture/picture_process.h` at line 6. We know `picture_process.h` includes `pack/pack_service.h` (line 5 of that file). So `pack::PackPlan` and `appctx::AppContext` are available through this include. **pipeline.cpp needs NO changes.**

#### 5. `tests/packer_tests.cpp`
- **ADD** after the last `#include` (line 11 `#include <vector>`), before line 13:  
  ```cpp
  using namespace pack::detail;
  ```
- **NO include changes needed**: includes `pack/packer.h` (line 1) which now provides pack_types.h + packer_types.h. The test file uses `PackGroupInput` at 16 sites (lines 56-171).
- Test file does NOT use `PackPlan` or `pack::` free functions — no need for pack_service.h.

### Files verified as needing NO changes:
| File | Reason |
|------|--------|
| `src/pack/pack_service.cpp` | Already includes both pack_service.h (line 1) and packer.h (line 7) — both now include pack_types.h. No moved types used directly. |
| `src/video/video_process.cpp` | Includes pack_service.h (line 11) — now provides pack_types.h transitively. Does not use moved types. |
| `src/picture/picture_process.h` | Includes pack_service.h (line 5) — PackPlan stayes in pack_service.h. No change. |
| `src/core/archive_plan.h` | Includes pack_service.h (line 4) — PackPlan stays. No change. |
| `src/app/pipeline.cpp` | Gets PackPlan + AppContext through picture_process.h / video_process.h. No change. |
| `tests/pack_service_tests.cpp` | Includes pack_service.h (line 2) — now provides pack_types.h. No moved types used. |

### Summary of actual file changes:
| File | Change |
|------|--------|
| `src/pack/packer.cpp` | Add `using namespace pack::detail;` after includes |
| `src/video/video_output_planning.cpp` | Add `using namespace pack::detail;` after includes |
| `src/picture/picture_process.cpp` | Add `using namespace pack::detail;` after includes |
| `tests/packer_tests.cpp` | Add `using namespace pack::detail;` after includes |
</action>

<acceptance_criteria>
- `grep "using namespace pack::detail" src/pack/packer.cpp` → 1 match
- `grep "using namespace pack::detail" src/video/video_output_planning.cpp` → 1 match
- `grep "using namespace pack::detail" src/picture/picture_process.cpp` → 1 match
- `grep "using namespace pack::detail" tests/packer_tests.cpp` → 1 match
- `grep "struct PackGroupInput\b" src/ --include="*.cpp" -r` → should still find matches (in comments or if qualified uses remain) but the struct definition is only in `packer_types.h`
- `grep -r "struct PackGroupInput\b" src/ --include="*.h"` → 0 matches outside `src/pack/packer_types.h`
- No consumer file has logic changes beyond `#include` and `using` — verify: `git diff --stat` shows only the 4 files above plus the new/modified headers
- `xmake build` compiles all targets successfully
</acceptance_criteria>

</task>

<task id="T06" type="execute" depends_on="T03,T04,T05">

<read_first>
- src/pack/packer.h (modified from T03 — no longer includes pack_service.h)
- xmake.lua: line 70 `add_files("tests/*.cpp")` — confirms test files are auto-picked up
- CONTEXT.md: D-03 (standalone compile test required)
</read_first>

<action>
Create NEW FILE `tests/packer_standalone_compile_test.cpp`:

```cpp
// Standalone compile test (D-03): verifies packer.h compiles WITHOUT pack_service.h
// This proves the circular dependency packer.h → pack_service.h is broken.
// If this file compiles, D-03 passes. The compilation step IS the test.

#include "pack/packer.h"

// packer.h must NOT transitively include pack_service.h.
// Types PackFileEntry, PackRunResult, FileOrdinalRange must be reachable via pack_types.h.
// Types PackGroupInput, PackEntryInput etc. must be reachable via pack::detail:: in packer_types.h.
// All packer.h free function declarations must compile without pack_service.h visibility.
```

Build integration:
- xmake.lua `tests` target line 70: `add_files("tests/*.cpp")` auto-picks up this file
- File compiles AND links into the test executable (alongside catch2)
- The compilation itself is the D-03 verification — if `xmake build tests` succeeds, the circular dependency is proven broken
- No test cases registered (Catch2 auto-registration skipped — no `TEST_CASE` macro)
</action>

<acceptance_criteria>
- `tests/packer_standalone_compile_test.cpp` exists
- Contains only `#include "pack/packer.h"` as its sole meaningful include (comments allowed)
- Does NOT contain `#include "pack/pack_service.h"` or any other pack/ include
- `grep '#include "pack/' tests/packer_standalone_compile_test.cpp` → exactly 1 match: `"pack/packer.h"`
- `xmake build tests` compiles and links this file successfully — definitive D-03 pass
- Full test suite (`xmake run tests`): 909 assertions pass (TYPE-04)
</acceptance_criteria>

</task>

</tasks>

<must_haves>
- `src/pack/pack_types.h` exists with PackFileEntry, FileOrdinalRange, PackRunResult, kDefaultMaxArchiveGroupSize in `pack::` namespace
- `src/pack/packer_types.h` exists with PackGroupInput, PackGroupPartition, PackEntryInput, PackEntryPartition in `pack::detail::` namespace
- `packer.h` no longer contains `#include "pack/pack_service.h"` — circular dependency broken (TYPE-03)
- `pack_service.h` includes `pack_types.h` (D-02 compliant)
- Global-scope aliases (`using ZipEntryNameResolver`, `using PackEntryProgressCallback`) removed from `packer.h` — now in `pack::detail::` (D-01)
- `kDefaultMaxArchiveGroupSize` moved from `packer.h` to `pack_types.h` (D-04)
- All consumer files compile with updated includes/using declarations
- Standalone compile test (`tests/packer_standalone_compile_test.cpp`) compiles successfully (D-03)
- Full test suite: 909 assertions, 215 test cases, zero failures, zero behavioral change (TYPE-04)
</must_haves>

<success_criteria>
1. All shared value types (PackFileEntry, FileOrdinalRange, PackRunResult) defined in `src/pack/pack_types.h` — usable without `pack_service.h` or `packer.h` (TYPE-01)
2. Global-scope structs accessible only through `pack::detail::` namespace in `src/pack/packer_types.h` (TYPE-02)
3. `packer.h` compiles without `pack_service.h` — circular dependency resolved, verified by standalone compile test (TYPE-03)
4. Full test suite passes — 909 assertions across 215 test cases with zero failures (TYPE-04)
5. All existing consumer code compiles unchanged — only `#include` paths adjusted, no logic modified
</success_criteria>

<verification>
### Build
1. `xmake build` — all targets compile and link successfully
2. `xmake build tests` — test target compiles (includes standalone compile test)

### Tests
3. `xmake run tests` — 909 assertions pass, 215 test cases pass, zero failures

### Circular Dependency
4. `grep '#include "pack/pack_service.h"' src/pack/packer.h` → empty
5. `tests/packer_standalone_compile_test.cpp` compiles as part of test target

### Header Audit
6. `grep -r "struct PackFileEntry\b" src/ --include="*.h"` → match ONLY in `pack_types.h`
7. `grep -r "struct PackRunResult\b" src/ --include="*.h"` → match ONLY in `pack_types.h`
8. `grep -r "struct FileOrdinalRange\b" src/ --include="*.h"` → match ONLY in `pack_types.h`
9. `grep -r "struct PackGroupInput\b" src/ --include="*.h"` → match ONLY in `packer_types.h`

### Global Scope Cleanup
10. `grep "^\s*(using|struct)\s" src/pack/packer.h` → empty (no unscoped declarations)
11. `grep "ZipEntryNameResolver\|PackEntryProgressCallback" src/pack/packer.h` → matches only within `namespace pack::detail { }` block

### Consumer Integrity
12. `git diff --stat src/video/ src/picture/ src/app/ src/core/` → only the files listed in T05 changed
13. Zero logic changes in any file — verify: `git diff` contains only `#include`, `using namespace`, and namespace qualification changes
</verification>
