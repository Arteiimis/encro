---
phase: 11
plan: "11-01-migrate-consumers-remove-facade"
type: execute
wave: 1
depends_on: []
files_modified:
  - src/core/archive_plan.h
  - src/core/archive_plan.cpp
  - src/picture/picture_process.h
  - src/picture/picture_process.cpp
  - src/app/pipeline.cpp
  - src/video/video_output_planning.cpp
  - src/video/video_process.cpp
  - src/pack/pack_facade.h (DELETE)
autonomous: true
requirements:
  - MIG-01
  - MIG-02
  - MIG-03
  - MIG-04
  - MIG-05
  - MIG-06
---
# Plan 11-01: Migrate All 7 Consumers to OO API + Delete Facade

## Objective

Migrate every consumer of `pack_facade.h` to use `PackService` + `Packer` OO API directly, delete the facade layer, and verify zero regressions (945 assertions, 8 E2E CLI paths).

## Tasks

---

### Task 1: Migrate archive_plan.h include

<task>
  <action>
    In `src/core/archive_plan.h` line 4, replace:
    `#include "pack/pack_facade.h"`
    with:
    `#include "pack/pack_types.h"`

    This header only needs the `PackPlan` type (used as parameter in `prepareResumablePackExecution`). Per D-04, headers that only need PackPlan type use `pack_types.h`.
  </action>
  <read_first>
    - src/core/archive_plan.h (current file)
    - src/pack/pack_types.h (verify PackPlan is defined there)
  </read_first>
  <acceptance_criteria>
    - grep result: `src/core/archive_plan.h` contains `#include "pack/pack_types.h"` (exact match)
    - grep result: `src/core/archive_plan.h` does NOT contain `pack_facade` (negative match)
    - No other lines changed in archive_plan.h
  </acceptance_criteria>
</task>

---

### Task 2: Migrate archive_plan.cpp

<task>
  <action>
    In `src/core/archive_plan.cpp`:
    
    **Includes:**
    Add after existing includes (line 2 area):
    `#include "pack/pack_service.h"`

    **Facade call replacements (3 call sites):**

    Line 32: Replace `pack_facade::resolveZipNameForIndex(plan, index)`
    with `pack::PackService::resolveZipNameForIndex(plan, index)`

    Line 33: Replace `pack_facade::resolveProgressLabelForIndex(plan, index)`
    with `pack::PackService::resolveProgressLabelForIndex(plan, index)`

    Line 66: Replace `pack_facade::selectPackPlanIndexes(plan, executionState->pendingIndexes)`
    with `pack::PackService::selectPackPlanIndexes(plan, executionState->pendingIndexes)`

    All three are static methods on PackService — no instance needed.
    No other lines changed.
  </action>
  <read_first>
    - src/core/archive_plan.cpp (current file)
    - src/pack/pack_service.h (verify `resolveZipNameForIndex`, `resolveProgressLabelForIndex`, `selectPackPlanIndexes` are static)
  </read_first>
  <acceptance_criteria>
    - grep result: `src/core/archive_plan.cpp` does NOT contain `pack_facade`
    - grep result: `src/core/archive_plan.cpp` contains `#include "pack/pack_service.h"`
    - grep result: `src/core/archive_plan.cpp` contains `pack::PackService::resolveZipNameForIndex(plan, index)`
    - grep result: `src/core/archive_plan.cpp` contains `pack::PackService::resolveProgressLabelForIndex(plan, index)`
    - grep result: `src/core/archive_plan.cpp` contains `pack::PackService::selectPackPlanIndexes(plan, executionState->pendingIndexes)`
    - No other call-site lines changed (verified via `git diff` — only the 3 call sites and include differ)
  </acceptance_criteria>
</task>

---

### Task 3: Migrate picture_process.h include

<task>
  <action>
    In `src/picture/picture_process.h` line 5, replace:
    `#include "pack/pack_facade.h"`
    with:
    `#include "pack/pack_types.h"`

    This header only needs `PackPlan` type for the return type of `buildPicturePackPlan`. Per D-04, headers that only need PackPlan type use `pack_types.h`.
  </action>
  <read_first>
    - src/picture/picture_process.h (current file)
    - src/pack/pack_types.h (verify PackPlan is defined there)
  </read_first>
  <acceptance_criteria>
    - grep result: `src/picture/picture_process.h` contains `#include "pack/pack_types.h"` (exact match)
    - grep result: `src/picture/picture_process.h` does NOT contain `pack_facade`
    - No other lines changed in picture_process.h
  </acceptance_criteria>
</task>

---

### Task 4: Migrate picture_process.cpp (8 calls, heaviest consumer)

<task>
  <action>
    In `src/picture/picture_process.cpp`:

    **Includes (line 8):**
    Replace line 8:
    `#include "pack/pack_facade.h"`
    with two includes:
    ```
    #include "pack/pack_service.h"
    #include "pack/packer.h"
    ```

    **Facade call replacements:**

    (A) Lines 443-448 — `runPicturePackWorkflow` compress branch:
    Replace `pack_facade::groupPackEntriesWithSubparts(` with:
    ```
    pack::Packer packer;
    packer.groupPackEntriesWithSubparts(
    ```
    (Create stack-local `pack::Packer` per D-03 before the call on line 443.)

    (B) Line 465 — Same function:
    Replace `pack_facade::buildGroupOrdinalRanges(groupedPics)`
    with `pack::PackService::buildGroupOrdinalRanges(groupedPics)`
    (Static method — no instance needed.)

    (C) Line 486 — Same function:
    Replace `pack_facade::runPackPlan(ctx, plan)`
    with:
    ```
    pack::PackService svc(std::make_unique<pack::Packer>());
    svc.runPackPlan(ctx, plan)
    ```
    (Per D-03 — stack-local instance created just before the call.)

    (D) Line 513 — `runPicturePackWorkflow` non-compress branch:
    Replace `pack_facade::runPackPlan(ctx, prepared.plan)`
    with:
    ```
    pack::PackService svc(std::make_unique<pack::Packer>());
    svc.runPackPlan(ctx, prepared.plan)
    ```

    (E) Line 539 — `packAllPicsToZip` function:
    Replace `pack_facade::packGroups(prepared.plan)`
    with:
    ```
    pack::PackService svc(std::make_unique<pack::Packer>());
    svc.packGroups(prepared.plan)
    ```

    (F) Lines 578-582 — `buildPicturePackPlan` function:
    Replace `pack_facade::groupPackEntriesWithSubparts(` with:
    ```
    pack::Packer packer;
    packer.groupPackEntriesWithSubparts(
    ```

    (G) Line 598 — Same function:
    Replace `pack_facade::buildGroupOrdinalRanges(groupedPics)`
    with `pack::PackService::buildGroupOrdinalRanges(groupedPics)`

    **using directive (line 24):**
    `using namespace pack::detail;` STAYS — needed for `PackEntryInput`, `PackEntryPartition` usage throughout the file.

    **Instance placement:**
    Per D-03, each function creates its own stack-local instances. The Packer/PackService declarations should appear just BEFORE their first use in each function (not at file scope).
  </action>
  <read_first>
    - src/picture/picture_process.cpp (current file — all 619 lines)
    - src/pack/pack_service.h (verify runPackPlan, packGroups, buildGroupOrdinalRanges signatures)
    - src/pack/packer.h (verify groupPackEntriesWithSubparts signature)
    - src/pack/pack_types.h (verify PackPlan, PackFileEntry, FileOrdinalRange types)
  </read_first>
  <acceptance_criteria>
    - grep result: `src/picture/picture_process.cpp` does NOT contain `pack_facade`
    - grep result: `src/picture/picture_process.cpp` contains `#include "pack/pack_service.h"`
    - grep result: `src/picture/picture_process.cpp` contains `#include "pack/packer.h"`
    - grep result: `src/picture/picture_process.cpp` contains `packer.groupPackEntriesWithSubparts(` (2 occurrences: compress branch + buildPicturePackPlan)
    - grep result: `src/picture/picture_process.cpp` contains `packer.groupPackEntriesWithSubparts(` — exactly 2 occurrences
    - grep result: `src/picture/picture_process.cpp` contains `svc.runPackPlan(ctx, plan)` (exact, 1 occurrence, line ~486)
    - grep result: `src/picture/picture_process.cpp` contains `svc.runPackPlan(ctx, prepared.plan)` (exact, 1 occurrence, line ~513)
    - grep result: `src/picture/picture_process.cpp` contains `svc.packGroups(prepared.plan)` (exact, 1 occurrence, line ~539)
    - grep result: `src/picture/picture_process.cpp` contains `pack::PackService::buildGroupOrdinalRanges(groupedPics)` (exact, 2 occurrences)
    - grep result: `src/picture/picture_process.cpp` contains `using namespace pack::detail;` (unchanged)
    - Build compiles without errors
  </acceptance_criteria>
</task>

---

### Task 5: Migrate pipeline.cpp

<task>
  <action>
    In `src/app/pipeline.cpp`:

    **Include (line 5):**
    Replace: `#include "pack/pack_facade.h"`
    with: `#include "pack/pack_service.h"`

    **Facade call replacement (line 51, `runPackOnly` function):**
    Replace:
    `return pack_facade::runDirectoryPackWorkflow(ctx, ctx.config.inputPath);`
    with:
    ```
    pack::PackService svc(std::make_unique<pack::Packer>());
    return svc.runDirectoryPackWorkflow(ctx, ctx.config.inputPath);
    ```
    (Per D-03 — stack-local instance created just before the call. The `svc` declaration and the `return` must be on separate lines.)

    No other lines changed.
  </action>
  <read_first>
    - src/app/pipeline.cpp (current file)
    - src/pack/pack_service.h (verify `runDirectoryPackWorkflow` is an instance method with signature `(appctx::AppContext&, const fs::path&) -> eh::Result<int>`)
  </read_first>
  <acceptance_criteria>
    - grep result: `src/app/pipeline.cpp` does NOT contain `pack_facade`
    - grep result: `src/app/pipeline.cpp` contains `#include "pack/pack_service.h"`
    - grep result: `src/app/pipeline.cpp` contains `svc.runDirectoryPackWorkflow(ctx, ctx.config.inputPath)` (exact)
    - grep result: `src/app/pipeline.cpp` contains `pack::PackService svc(std::make_unique<pack::Packer>())` (exact)
    - No other lines changed in pipeline.cpp (verified via `git diff`)
  </acceptance_criteria>
</task>

---

### Task 6: Migrate video_output_planning.cpp

<task>
  <action>
    In `src/video/video_output_planning.cpp`:

    **Include (line 4):**
    Replace: `#include "pack/pack_facade.h"`
    with: `#include "pack/packer.h"`

    **Facade call replacements (2 overloads of `groupEncodedVideosForPack`):**

    (A) Lines 185-186 — first overload (takes `vector<fs::path>`):
    Replace:
    ```
    return pack_facade::groupPackFiles(packInputs, pack::kDefaultMaxArchiveGroupSize);
    ```
    with:
    ```
    pack::Packer packer;
    return packer.groupPackFiles(packInputs, pack::kDefaultMaxArchiveGroupSize);
    ```

    (B) Lines 200-205 — second overload (takes `vector<EncodedVideoPackFile>`):
    Replace:
    ```
    return pack_facade::groupPackFiles(
      packInputs,
      pack::kDefaultMaxArchiveGroupSize,
      std::nullopt,
      keepSourceDirsTogetherWhenTotalFilesExceed
    );
    ```
    with:
    ```
    pack::Packer packer;
    return packer.groupPackFiles(
      packInputs,
      pack::kDefaultMaxArchiveGroupSize,
      std::nullopt,
      keepSourceDirsTogetherWhenTotalFilesExceed
    );
    ```

    **using directive (line 14):**
    `using namespace pack::detail;` STAYS — needed for `PackGroupInput` usage (lines 179, 182, 195).

    Per D-02: This file only needs Packer grouping (no orchestration). Per D-03: each function gets its own stack-local Packer.
  </action>
  <read_first>
    - src/video/video_output_planning.cpp (current file)
    - src/pack/packer.h (verify `groupPackFiles` signature — Packer method, not virtual)
  </read_first>
  <acceptance_criteria>
    - grep result: `src/video/video_output_planning.cpp` does NOT contain `pack_facade`
    - grep result: `src/video/video_output_planning.cpp` contains `#include "pack/packer.h"`
    - grep result: `src/video/video_output_planning.cpp` contains `packer.groupPackFiles(packInputs, pack::kDefaultMaxArchiveGroupSize)` (first overload)
    - grep result: `src/video/video_output_planning.cpp` contains `packer.groupPackFiles(` — exactly 2 occurrences
    - grep result: `src/video/video_output_planning.cpp` contains `using namespace pack::detail;` (unchanged)
    - No other lines changed (verified via `git diff`)
  </acceptance_criteria>
</task>

---

### Task 7: Migrate video_process.cpp

<task>
  <action>
    In `src/video/video_process.cpp`:

    **Include (line 11):**
    Replace: `#include "pack/pack_facade.h"`
    with: `#include "pack/pack_service.h"`

    **Facade call replacements (in `packEncodedVideos` function, lines 374-448, inside anonymous namespace):**

    (A) Line 406:
    Replace `pack_facade::buildGroupOrdinalRanges(groupedFiles)`
    with `pack::PackService::buildGroupOrdinalRanges(groupedFiles)`
    (Static method — no instance needed.)

    (B) Line 428-431:
    Replace `pack_facade::appendOrdinalRangeSuffix(`
    with `pack::PackService::appendOrdinalRangeSuffix(`
    (Static method — only the namespace changes, the call arguments stay the same.)

    (C) Line 437:
    Replace `pack_facade::runPackPlan(ctx, plan)`
    with:
    ```
    pack::PackService svc(std::make_unique<pack::Packer>());
    svc.runPackPlan(ctx, plan)
    ```
    (Per D-03 — stack-local instance created just before the call.)

    No other lines changed.
  </action>
  <read_first>
    - src/video/video_process.cpp (current file)
    - src/pack/pack_service.h (verify `buildGroupOrdinalRanges`, `appendOrdinalRangeSuffix` are static, `runPackPlan` is instance method)
  </read_first>
  <acceptance_criteria>
    - grep result: `src/video/video_process.cpp` does NOT contain `pack_facade`
    - grep result: `src/video/video_process.cpp` contains `#include "pack/pack_service.h"`
    - grep result: `src/video/video_process.cpp` contains `pack::PackService::buildGroupOrdinalRanges(groupedFiles)` (exact)
    - grep result: `src/video/video_process.cpp` contains `pack::PackService::appendOrdinalRangeSuffix(` (exact)
    - grep result: `src/video/video_process.cpp` contains `svc.runPackPlan(ctx, plan)` (exact)
    - grep result: `src/video/video_process.cpp` contains `pack::PackService svc(std::make_unique<pack::Packer>())` (exact)
  </acceptance_criteria>
</task>

---

### Task 8: DELETE pack_facade.h

<task>
  <action>
    Delete the file `src/pack/pack_facade.h` entirely.

    This is the final step — the facade layer is fully removed. All consumers have been migrated in Tasks 1-7.
    
    Verify after deletion that the build still succeeds (proves no transitive includes relied on this file).
  </action>
  <read_first>
    - Verify all Tasks 1-7 are complete and consumer files compile
  </read_first>
  <acceptance_criteria>
    - File `src/pack/pack_facade.h` does NOT exist on disk
    - `rg "pack_facade" --type cpp src/` returns zero results (except possibly comments/strings)
    - `rg "pack_facade" --type cpp tests/` returns zero results
    - Full build (`xmake build`) succeeds
  </acceptance_criteria>
</task>

---

### Task 9: Include Audit & Cleanup

<task>
  <action>
    Run a systematic include audit across all consumer files:

    **Header policy (D-04):**
    - `.h` files must only include `pack/pack_types.h` (never `pack_service.h` or `packer.h`)
    
    **Verification checklist:**

    1. `src/core/archive_plan.h` — has `#include "pack/pack_types.h"`, no `pack_service.h` or `packer.h`
    2. `src/picture/picture_process.h` — has `#include "pack/pack_types.h"`, no `pack_service.h` or `packer.h`
    3. `src/core/archive_plan.cpp` — includes `pack/pack_service.h` (for static methods)
    4. `src/picture/picture_process.cpp` — includes `pack/pack_service.h` and `pack/packer.h`
    5. `src/app/pipeline.cpp` — includes `pack/pack_service.h`
    6. `src/video/video_output_planning.cpp` — includes `pack/packer.h`
    7. `src/video/video_process.cpp` — includes `pack/pack_service.h`

    **Unused include check:**
    - No `.h` file includes `pack_service.h` or `packer.h` transitively (verified by checking each header)
    
    **using directive review:**
    - `picture_process.cpp:24` — `using namespace pack::detail;` → Keep (needed for PackEntryInput, PackEntryPartition)
    - `video_output_planning.cpp:14` — `using namespace pack::detail;` → Keep (needed for PackGroupInput)
    
    **Dead code:** None expected — all migrated code is active. The only removed code is `pack_facade.h`.
  </action>
  <read_first>
    - All 7 consumer files (fully migrated state)
    - src/pack/pack_types.h (verify it has PackPlan, PackFileEntry, FileOrdinalRange, PackRunResult, PackProgressCallbacks)
    - src/pack/packer.h (verify it includes packer_types.h)
    - src/pack/packer_types.h (verify PackGroupInput, PackEntryInput, PackGroupPartition, PackEntryPartition in `pack::detail`)
  </read_first>
  <acceptance_criteria>
    - Zero `#include "pack/pack_facade.h"` in any file (`rg "#include \"pack/pack_facade.h\"" src/ tests/` returns empty)
    - `rg "#include \"pack/pack_facade.h\"" src/ tests/` returns zero results
    - Both `.h` consumer files (`archive_plan.h`, `picture_process.h`) contain `#include "pack/pack_types.h"`
    - No `.h` consumer file contains `#include "pack/pack_service.h"` or `#include "pack/packer.h"`
    - `using namespace pack::detail;` appears in exactly 2 files: `picture_process.cpp` and `video_output_planning.cpp`
    - All consumer `.cpp` files have the correct pack includes per the checklist above
  </acceptance_criteria>
</task>

---

### Task 10: Build & Test Verification

<task>
  <action>
    Build the project and run the full test suite. All 945 assertions must pass with zero failures.

    Run:
    ```
    xmake build
    xmake run tests
    ```

    Verify:
    - Build exits 0 with no errors
    - All 225 test cases pass
    - 945 assertions pass (zero failures)
    - No deprecation warnings about pack_facade (since it's deleted, none can remain)

    If build fails: check compiler errors, fix missing includes or signature mismatches.
    If tests fail: compare with pre-migration results, identify regressions.
  </action>
  <read_first>
    - xmake.lua (build config)
    - Any test runner entry point
  </read_first>
  <acceptance_criteria>
    - `xmake build` exits 0
    - Test output shows "All tests passed" or equivalent
    - Assertion count: 945 passed, 0 failed (grep test output for "assertions" or count via test runner)
    - Test case count: 225 passed (grep test output for "test cases")
    - Zero compiler warnings related to `pack_facade` or deprecated functions
  </acceptance_criteria>
</task>

---

### Task 11: E2E CLI Verification (8 Paths)

<task>
  <action>
    Verify 8 E2E CLI workflow paths produce correct output. Per D-05, all paths should produce output identical to pre-migration v1.2 baseline.

    For each test path, run the CLI with representative input and verify:
    1. Exit code is 0
    2. Output files exist in expected locations
    3. Log output matches baseline (excluding timestamps)

    **Test paths:**

    1. `--pack` (encode videos + pack default):
       ```
       encro --pack -i <test_video_dir>
       ```

    2. `--type picture` (process pictures):
       ```
       encro --type picture -i <test_picture_dir>
       ```

    3. `--pack-only` (pack only, no encoding):
       ```
       encro --pack-only -i <test_dir>
       ```

    4. `--full-progress` (verbose per-worker bars):
       ```
       encro --full-progress --pack -i <test_video_dir>
       ```

    5. `--resume` (resume interrupted job):
       ```
       encro --resume -i <test_video_dir>
       ```

    6. `--restart` (restart from scratch):
       ```
       encro --restart -i <test_video_dir>
       ```

    7. Conflicting filenames / overwrite prompt:
       ```
       encro --pack -i <test_dir_with_duplicates>
       ```

    8. `--flat` output directory structure:
       ```
       encro --flat --type picture -i <test_picture_dir>
       ```

    Each test must exit 0 and produce expected output files.
  </action>
  <read_first>
    - src/app/cli.cpp or main entry point (understanding CLI argument handling)
    - CONTEXT.md D-05 (E2E verification scope)
  </read_first>
  <acceptance_criteria>
    - CLI path 1 (`--pack`) exits 0, produces zip archives in `packed/` subdirectory
    - CLI path 2 (`--type picture`) exits 0, produces zip archives
    - CLI path 3 (`--pack-only`) exits 0, produces zip archives with video files
    - CLI path 4 (`--full-progress`) exits 0, produces zip archives with full progress output
    - CLI path 5 (`--resume`) exits 0, handles resume correctly
    - CLI path 6 (`--restart`) exits 0, reconstructs job state
    - CLI path 7 (conflicting names) exits 0, handles conflicts per collision naming
    - CLI path 8 (`--flat`) exits 0, produces flat-structured zip entries
  </acceptance_criteria>
</task>

## Verification Summary

1. **Include Audit:** Zero `#include "pack/pack_facade.h"` in entire codebase (Task 9)
2. **Header Policy:** `.h` consumers use `pack_types.h` only; `.cpp` consumers include only what they use (Task 9)
3. **Build:** `xmake build` exits 0 with zero warnings (Task 10)
4. **Tests:** 945 assertions pass, 225 test cases, zero failures (Task 10)
5. **Consumer Diffs:** `git diff src/video/ src/picture/ src/core/ src/app/` shows only API migration changes — no logic touched (verify visually)
6. **E2E CLI:** 8 paths produce correct output (Task 11)
7. **File Deletion:** `src/pack/pack_facade.h` no longer exists (Task 8)

## Success Criteria (from CONTEXT.md)

| # | Criterion | Target |
|---|-----------|--------|
| 1 | Zero `#include "pack/pack_facade.h"` | `rg` returns empty |
| 2 | `pack_facade.h` deleted | File absent |
| 3 | All consumers compile with OO API | Build exits 0 |
| 4 | 945 assertions pass | 225 tests, 0 failures |
| 5 | Only API migration changes | `git diff` review |
| 6 | 8 E2E paths produce identical output | CLI verification |
| 7 | No unused pack includes | Include audit |
| 8 | `using namespace pack::detail` reviewed | 2 files, both justified |

## Deferred

None — Phase 11 is the final phase of v1.3. After completion, the pack subsystem is fully OO with DI support.

*Phase: 11-consumer-migration-cleanup*
*Plan: 11-01-migrate-consumers-remove-facade*
*Created: 2026-04-30*
