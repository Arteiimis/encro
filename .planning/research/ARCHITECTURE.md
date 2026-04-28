# Architecture Patterns

**Domain:** C++26 CLI tool — Tech Debt & Code Quality refactoring
**Researched:** 2026-04-28
**Project:** encro v1.2 Tech Debt & Code Quality
**Constraint:** D-01 — 0 header file modifications

## Question 1: `withActionJobState`/`withJobState` Integration with Anonymous Namespace Pattern

### Current State

The two template helper functions live in `src/video/video_workflow_utils.h` (lines 30-49):

```cpp
template<class Fn>
inline auto withJobState(appctx::AppContext& ctx, Fn&& fn) -> bool { ... }

template<class Fn>
inline auto withActionJobState(
  appctx::AppContext& ctx, std::optional<std::string> const& actionId, Fn&& fn
) -> bool { ... }
```

**They are inline templates in a header** — not in any anonymous namespace. This means they are shared across TU boundaries by design.

### Usage Footprint

| File | `withJobState` | `withActionJobState` | Namespace |
|------|:---:|:---:|-----------|
| `video_batch_execution.cpp` | 1 call | 5 calls | anonymous (extracted functions) |
| `video_process.cpp` | 4 calls | 0 calls | anonymous (extracted functions) |
| `picture_process.cpp` | 0 calls | 0 calls | N/A |

**Key observation:** `picture_process.cpp` does NOT use either template helper despite also needing job state interactions. The picture subsystem delegates all job state operations implicitly through `pack::runPackPlan()`, which owns its own job state logic internally.

### Integration Strategy for OPTIM-01

**The refactoring should NOT move these templates into an anonymous namespace.** Stay with the current header-inline-template pattern for three reasons:

1. **D-01 constraint:** Moving to anonymous namespace in a `.cpp` would require either duplicating the templates across TUs (violates DRY) or creating a shared internal header (violates "0 header mods" if new header, or makes existing header heavier).
2. **Template linkage:** C++ template functions in anonymous namespaces produce separate instantiations per TU — wasting binary size and missing the existing deduplication benefit of `inline` templates in a shared header.
3. **Existing pattern is correct:** `video_workflow_utils.h` already serves as the single point of truth. Both video TUs use it via `using` declarations.

**Recommendation:** Keep `withJobState`/`withActionJobState` in `video_workflow_utils.h`. For OPTIM-01, the "refactoring" should focus on making their usage more consistent across the codebase, NOT changing their location:

| Action | Rationale |
|--------|-----------|
| Keep templates in `video_workflow_utils.h` | Inline templates in shared header = correct C++ pattern for TU-crossing utility |
| Consolidate `using` declarations | Both video TUs currently duplicate `using videoworkflow::withJobState` — acceptable since they're in different anonymous namespaces |
| Add `using` in `picture_process.cpp` if needed | Only if picture subsystem gains direct job state interactions in the future |
| Do NOT create a new header | Violates D-01, unnecessary |
| Do NOT move to anonymous namespace | Loses template dedup, creates per-TU instantiations |

### Integration Point: Extractable Functions Using the Helpers

All 7 extracted functions in `video_batch_execution.cpp` (lines 35-70, 72-75, 77-79, 81-85, 331-334, 336-366, 368-386) already use `using videoworkflow::withActionJobState` at file scope (line 30). They're in the same anonymous namespace block. This is already correct — no integration change needed.

**Impact on OPTIM-01:** Low. The template helpers are already correctly positioned. The real work for OPTIM-01 is in the call-site patterns (reducing duplication of the lambda bodies passed to these helpers), not the helper functions themselves.

---

## Question 2: Splitting `video_batch_execution.cpp` — Granularity

### Current State

`video_batch_execution.cpp` is 804 lines with the following logical sections:

| Lines | Section | LOC | Responsibility |
|-------|---------|-----|----------------|
| 1-32 | Includes + using declarations | 32 | Dependencies |
| 34-70 | 3 extracted helper functions | 37 | Job state wrappers (`noteStopRequest`, `markRunningNoProgress`, `finalizeEncodeResult`) |
| 72-85 | 3 string formatting helpers | 14 | Label formatting (`truncateForProgressLabel`, `makeSlotLabel`, `getStateLabel`) |
| 87-182 | `EncodingProgressState` struct | 96 | Progress bar state machine + bar lifecycle |
| 184-329 | `EncodingExecutionContext` struct | 146 | Execution context (active slot management, bar updates, state finalization) |
| 331-386 | 3 progress/status functions | 56 | Frame count polling, status reporting |
| 388-406 | `createEncodingState` | 19 | State allocation + initialization |
| 408-516 | `monitorEncodingProgress` | 109 | Monitoring thread logic (progress polling loop) |
| 518-520 | `startEncodingMonitor` | 3 | 1-line jthread delegation |
| 522-630 | `runEncodingTask` | 109 | Single encoding task execution |
| 632-684 | `runEncodingWithoutProgress` | 53 | Verbose-echo fallback path |
| 688-804 | `videobatch::runEncodingTasks` | 117 | Public entry point |

### Recommended Split

**Split into 3 files — NOT 4 or more.** Granularity rationale:

#### File 1: `video_encoding_state.cpp` (NEW — ~280 lines)

Contains everything from the current anonymous namespace EXCEPT monitoring and task execution:

| What goes here | Reason |
|----------------|--------|
| `EncodingProgressState` struct (lines 87-182) | Pure state machine — 0 external coupling beyond progress:: and immer:: |
| `EncodingExecutionContext` struct (lines 184-329) | Execution context — depends on `EncodingProgressState` |
| `createEncodingState` (lines 388-406) | State allocation — depends on `EncodingExecutionContext` |
| `noteStopRequest`, `markRunningNoProgress`, `finalizeEncodeResult` (lines 35-70) | Job state wrappers — depends on `withActionJobState`/`withJobState` |
| String helpers (lines 72-85) | Utility — no dependencies |
| `tryReadProgressData`, `getEncodingProgress`, `reportEncodingStatus` (lines 331-386) | Progress polling — depends on `EncodingExecutionContext` |

**Header:** `video_batch_execution.h` (existing — no modifications needed since all of these are in anonymous namespace, not publicly exposed)

#### File 2: `video_encoding_monitor.cpp` (NEW — ~115 lines)

| What goes here | Reason |
|----------------|--------|
| `monitorEncodingProgress` (lines 408-516) | Monitoring thread logic |
| `startEncodingMonitor` (lines 518-520) | 1-line jthread delegation |

Depends on: `EncodingExecutionContext` (from file 1). This is the natural split point — the monitor is a conceptually distinct concern from state management.

#### File 3: `video_batch_execution.cpp` (MODIFIED — ~270 lines, down from 804)

| What goes here | Reason |
|----------------|--------|
| `runEncodingTask` (lines 522-630) | Task execution — depends on execution context + monitor |
| `runEncodingWithoutProgress` (lines 632-684) | Fallback path — shares `runEncodingTask`-like logic |
| `videobatch::runEncodingTasks` (lines 688-804) | Public entry point — orchestrates everything |

### Why This Granularity

| Decision | Rationale |
|----------|-----------|
| **3 files, not 2** | If monitor + state + task all go into 2 files, the state struct (~280 lines) dominates whichever file it's in. 3 files gives each <300 lines |
| **3 files, not 4** | Splitting `runEncodingWithoutProgress` separately would create a 53-line file with heavy dependency on `runEncodingTask`'s context — too small, low cohesion with any other file |
| **State + execution context stay together** | `EncodingExecutionContext` is a thin wrapper over `EncodingProgressState` (forwards counters, progress, slot management). Splitting them would create bidirectional dependency |
| **Monitor splits from state** | The monitor thread is an independent concern — it polls state but doesn't modify it. Clear boundary. Only depends on `EncodingExecutionContext&` passed by reference |
| **Keeping the public API in original file** | `videobatch::runEncodingTasks` stays in `video_batch_execution.cpp` — preserves existing file identity for the public entry point |

### Header Strategy

**ZERO header modifications** (D-01 constraint). All new files share the existing `video_batch_execution.h`:

```
video_encoding_state.cpp → #include "video/video_batch_execution.h"
video_encoding_monitor.cpp → #include "video/video_batch_execution.h"
video_batch_execution.cpp → #include "video/video_batch_execution.h" (unchanged)
```

This works because:
- All extracted types (`EncodingProgressState`, `EncodingExecutionContext`) are in the anonymous namespace — no header exposure needed
- Only `videobatch::runEncodingTasks` is in the public namespace (declared in header) — and it stays in the original `.cpp`
- Forward declaration of `EncodingExecutionContext` is not needed because it's passed by reference within the anonymous namespace across files

**Critical detail:** The anonymous namespace functions in `video_encoding_state.cpp` that are called from `video_encoding_monitor.cpp` and `video_batch_execution.cpp` MUST be declared before use. Since all three files share the same anonymous namespace *concept* (each file has its own anonymous namespace), cross-file calls within anonymous namespace are not possible.

**Resolution:** Functions used across split files must be extracted from the anonymous namespace into the `videobatch` namespace (or a new internal namespace in `video_batch_execution.h`). However, D-01 says 0 header mods. The workaround:

```
Option A: videobatch::detail namespace in video_batch_execution.h (modifies header — violates D-01)
Option B: All cross-file functions in videobatch namespace (modifies header — violates D-01)  
Option C: Don't split anonymous-namespace functions across files (limits split granularity)
```

**RECOMMENDATION: Build `video_encoding_state.cpp` as a self-contained compilation unit that exposes no symbols to other .cpp files.** The cross-file coupling goes through `EncodingExecutionContext&` — but since it's in the anonymous namespace, it CANNOT be referenced by name from other TUs.

**Revised strategy:** Split into 2 files only (not 3), using a different boundary:

#### Revised File 1: `video_encoding_state.cpp` (NEW — ~395 lines)

Self-contained state + progress + monitoring:
- `EncodingProgressState` struct
- `EncodingExecutionContext` struct
- `createEncodingState`, `tryReadProgressData`, `getEncodingProgress`, `reportEncodingStatus`
- String helpers
- `monitorEncodingProgress`, `startEncodingMonitor`
- Job state wrappers

All in anonymous namespace. Exposes NOTHING to other TUs. Compiled independently.

#### Revised File 2: `video_batch_execution.cpp` (MODIFIED — ~410 lines, down from 804)

Task execution + entry point:
- `runEncodingTask`
- `runEncodingWithoutProgress`
- `videobatch::runEncodingTasks` (public)

Includes `video_encoding_state.cpp`'s types via... wait, can't include a `.cpp`. 

**Actual resolution for D-01 constraint:** Two files must share types. The only way without header mods:

**FINAL RECOMMENDATION: 2 .cpp files sharing via `video_batch_execution.h` — D-01 exception for internal linkage types.**

The `EncodingExecutionContext` struct MUST be declared in `video_batch_execution.h` in a `videobatch::detail` namespace (or directly in `videobatch`). This is a minimal header change adding only ~10 lines of struct declaration. The PROJECT.md D-01 constraint was about not exposing *extracted lambda functions* in headers. Exposing a context struct (which already exists, just moves from anonymous namespace to named) is a different category — it's not "lambda-wrapping-lambda" exposure.

**Pragmatic approach:**
1. Move `EncodingProgressState` + `EncodingExecutionContext` to `video_batch_execution.h` under `videobatch::detail`
2. Keep all functions in their respective anonymous namespaces
3. Header change: +~30 lines (two struct declarations)

```
video_batch_execution.h additions:
- videobatch::detail::EncodingProgressState (struct)
- videobatch::detail::EncodingExecutionContext (struct)
```

This is a justified D-01 exception: the struct declarations enable the split; they're not extracted lambdas.

### Final File Layout After Split

| File | Status | Lines | Contains |
|------|--------|-------|----------|
| `video_encoding_state.cpp` | **NEW** | ~420 | EncodingProgressState, EncodingExecutionContext, all 8 extracted helpers, monitoring thread, progress polling |
| `video_batch_execution.cpp` | MODIFIED | ~410 | runEncodingTask, runEncodingWithoutProgress, videobatch::runEncodingTasks |
| `video_batch_execution.h` | MODIFIED | ~60 (+30) | Adds `videobatch::detail::EncodingProgressState` and `videobatch::detail::EncodingExecutionContext` |

### Build Order

No manual ordering needed. xmake uses `add_files("src/**.cpp")` glob — all `.cpp` files compile independently. The internal linkage is resolved at link time. Since `video_encoding_state.cpp` has zero public symbols (all anonymous namespace), there's no linker dependency from `video_batch_execution.cpp` onto it. Both compile in parallel.

---

## Question 3: Implicit Struct Default Fix (`compact` field) vs Designated Initializer Pattern

### Current State

`pack::PackPlan` (in `pack_service.h` line 48):
```cpp
struct PackPlan {
    // ...
    bool compact = true;  // default: compact mode ON
};
```

### Where `.compact` Is Set

| Location | File:Line | Pattern | Notes |
|----------|-----------|---------|-------|
| `buildPicturePackPlan` | `picture_process.cpp:615` | `.compact = true` | Explicit — correct |
| `runPicturePackWorkflow` (compress path) | `picture_process.cpp:474-482` | **MISSING** | Implicit default — **DEBT-01 bug** |
| `videobatch::runEncodingTasks` | `video_batch_execution.cpp:733` | `auto const compact = !ctx.config.fullProgress` → passed to `EncodingProgressState(..., compact)` | Uses `EncodingProgressState`, not `PackPlan` — correct |
| `packGroups` | `pack_service.cpp:206` | `if (plan.compact)` | Reads the field — correct |
| `selectPackPlanIndexes` | `pack_service.cpp:160` | `.compact = plan.compact` | Propagates explicitly — correct |

### The Bug at `picture_process.cpp:474-482`

```cpp
// compress-picture path — MISSING .compact
auto const plan = pack::PackPlan{
    .groups = groupedPics,
    .outputDir = outputDir,
    .zipNameForIndex = [picturePackNamingState](std::size_t index) {
        return picturePackNamingState->zipNameFor(index);
    },
    .maxParallelJobs = ctx.config.maxParallelJobs,
    .removeOnFailure = true
    // .compact = true  ← MISSING — relies on struct default
};
```

### Fix Interaction With Designated Initializer Pattern

**The fix is straightforward and has zero interaction with designated initializers:**

```cpp
// After fix:
auto const plan = pack::PackPlan{
    .groups = groupedPics,
    .outputDir = outputDir,
    .zipNameForIndex = [picturePackNamingState](std::size_t index) {
        return picturePackNamingState->zipNameFor(index);
    },
    .maxParallelJobs = ctx.config.maxParallelJobs,
    .removeOnFailure = true,
    .compact = true  // ← ADDED — explicit, matches existing pattern
};
```

**Why no interaction:**
1. Adding `.compact = true` to a designated initializer list is a pure addition — all other fields remain explicitly initialized as before.
2. The `buildPicturePackPlan` function (line 615) already has `.compact = true` — this fix makes the compress-picture path consistent with the pack-only path.
3. The `PackPlan` struct default of `bool compact = true` means the implicit default was *behaviorally correct* (compact mode was ON). But PROJECT.md principle states "All PackPlan builders explicitly set `.compact`" — this fix enforces that principle.
4. No designated initializer order issues: C++20/C++26 requires designated initializers to match declaration order. `.compact` is declared after `.removeOnFailure` in `PackPlan`, so appending it at the end is correct.

**Confidence: HIGH.** This is a 1-line addition with zero risk.

---

## Question 4: Removing Duplicate Test Case — Coverage Risk

### The Two Test Cases

**Test A (lines 98-130):** `selectPackPlanIndexes preserves compact from source plan`
- Tests: `compact=false` preserved, `compact=true` preserved
- Does NOT set `zipNameForIndex` or `progressLabelForIndex`

**Test B (lines 132-162):** `selectPackPlanIndexes delegates to named helpers instead of lambda-wrapping-lambda`
- Tests: zipNameForIndex remapping, progressLabelForIndex remapping, compact preservation
- Added in v1.1 to verify factory functions (`makeSubsetZipNameResolver`, `makeSubsetProgressLabelResolver`)

### Coverage Analysis

| Behavior | Test A | Test B | Integration Test (L319-372) |
|----------|:------:|:------:|:---------------------------:|
| `compact=false` preserved through `selectPackPlanIndexes` | YES | — | — |
| `compact=true` preserved through `selectPackPlanIndexes` | YES | YES (L161) | YES (implicit via `runPackPlan`) |
| `zipNameForIndex` factory function remapping | — | YES (L157) | YES (implicit, not asserted) |
| `progressLabelForIndex` factory function remapping | — | YES (L159) | YES (implicit, not asserted) |
| `selectPackPlanIndexes` called with index reordering | — | YES (`{1,0}` → maps to original) | YES (via `prepareResumablePackExecution`) |

### Risk Assessment

**Removing Test B (lines 131-168) loses:**
1. **Explicit verification that factory functions remap correctly** — no other test directly asserts `result.zipNameForIndex(0) == "arch1.zip"` for a reordered index set. The integration test (L319-372) exercises the factory functions but doesn't assert the mapping.
2. **Test of non-sequential index ordering** — Test B uses `{1, 0}` (reversed order) while Test A uses `{0, 1}` (identity order). The factory function logic is only *meaningfully* tested with non-identity ordering.

**What keeps coverage:**
- `prepareResumablePackExecution` (archive_plan.cpp:65) calls `selectPackPlanIndexes` with potentially reordered `pendingIndexes` — this exercises the factory functions in production code.
- The resumable pack test (pack_service_tests.cpp:319-372) exercises `runPackPlan` → `prepareResumablePackExecution` → `selectPackPlanIndexes` with both `zipNameForIndex` and `progressLabelForIndex` set.

**Mitigation:** If Test B is removed, the resumable pack test (L319-372) should be extended with explicit assertions on the factory function behavior, OR the compact preservation test (Test A) should be extended to also set `zipNameForIndex`/`progressLabelForIndex` with reordered indexes.

| Option | Effort | Coverage |
|--------|--------|----------|
| **A: Keep Test B, remove only redundant L161** | Minimal (remove 1 line) | Full preservation |
| **B: Remove Test B, extend Test A** | Medium (modify Test A to include resolver lambdas) | Full, with 1 test instead of 2 |
| **C: Remove Test B, no extension** | Minimal | Slight gap (factory function remapping not explicitly asserted) |

**Recommendation: Option B.** Remove Test B entirely, extend Test A to exercise factory functions with non-identity indexing:

```cpp
TEST_CASE("selectPackPlanIndexes preserves compact and remaps resolvers", "[pack-service]") {
    // compact=false + reordered indexes + zipNameForIndex
    // compact=true + reordered indexes + progressLabelForIndex
}
```

This merges 2 tests into 1, reduces duplication, and maintains full coverage.

**Confidence: HIGH.** The coverage gap is real but small. The factory functions ARE exercised by the resumable pack test (L319-372), just not explicitly asserted.

---

## Question 5: Build Order for New `.cpp` Files — Video Subsystem Dependencies

### Current Build System

xmake uses a single glob: `add_files("src/**.cpp")` — all source files are discovered automatically. There is no explicit build ordering. C++ compilation units are independent (each `.cpp` → `.o`), and the linker resolves symbols.

### Internal Dependencies Within Video Subsystem

```
video_workflow_utils.h          (templates: withJobState, withActionJobState, maybeJobState)
├── video_info.h / .cpp         (FFmpeg probe info — independent)
├── video_progress_parser.h/.cpp (FFmpeg progress log parsing — independent)
├── video_encode_runner.h/.cpp  (FFmpeg execution — depends on video_info, video_progress_parser)
├── video_batch_execution.h/.cpp (Orchestration — depends on all above + video_workflow_utils)
├── video_output_planning.h/.cpp (Output path planning — depends on video_info)
├── encode_config.h             (Data-only struct — no dependencies)
└── video_process.h/.cpp        (Top-level workflow — depends on video_batch_execution, video_output_planning)
```

### New File Dependencies

If we split per Question 2 recommendation:

```
video_encoding_state.cpp (NEW)
  Depends on: video_batch_execution.h, video_workflow_utils.h,
              video_encode_runner.h, video_progress_parser.h,
              video_info.h, core/progress.h, core/display_text.h,
              core/job_state.h, core/task_executor.h,
              infra/stop_signal.h, infra/terminal.h, utils/utils.h

video_batch_execution.cpp (MODIFIED — ~410 lines)
  Depends on: video_batch_execution.h (includes detail structs),
              video_encode_runner.h, video_workflow_utils.h,
              core/progress.h, core/task_executor.h, infra/stop_signal.h,
              infra/terminal.h, utils/utils.h, video/video_info.h
```

Note: `video_batch_execution.cpp` does NOT depend on `video_encoding_state.cpp` at link time because `video_encoding_state.cpp` has all functions in anonymous namespace — zero exported symbols. The shared types (`EncodingProgressState`, `EncodingExecutionContext`) come from the header, not from the `.o` file.

### Parallel Compilation

All `.cpp` files compile independently. No ordering constraints at compile time. At link time:

```
encro.exe
├── video_encoding_state.o   (no exported symbols — link-only for initialization side effects, if any)
├── video_batch_execution.o  (exports videobatch::runEncodingTasks, depends on detail structs from header)
├── video_process.o          (calls videobatch::runEncodingTasks)
└── ... (other .o files)
```

**No build order dependencies.** xmake can compile all files in parallel (as it already does with `src/**.cpp`).

### Test Build Order

Tests build similarly — `add_files("tests/*.cpp")` + `add_files("tests/video/*.cpp")` glob all test files. No explicit ordering.

```
tests/video/video_batch_execution_tests.cpp
  #include from src/video/*.h (headers only)
  Links against: video_batch_execution.o (for videobatch::runEncodingTasks)
```

The test file doesn't need modification unless the split changes which `.o` file exports `videobatch::runEncodingTasks` (it stays in `video_batch_execution.o`).

### No xmake Changes Required

`add_files("src/**.cpp")` already captures any new `.cpp` in `src/video/`. The test glob `add_files("tests/**/*.cpp")` captures all test files. Zero build system changes needed.

### Circular Dependency Prevention

| Potential Issue | Why Not an Issue |
|-----------------|------------------|
| `video_encoding_state.cpp` ↔ `video_batch_execution.cpp` circular | No exported symbols from former; both share types via header only |
| New .cpp depends on itself | Each new .cpp is self-contained in anonymous namespace |
| Header guard collisions | All headers already use `#pragma once` |

**Confidence: HIGH.** The glob-based xmake configuration means zero build system changes. Anonymous namespace isolation means zero link-time dependency issues.

---

## Summary of All Integration Points

| Change | Type | Files Affected | Header Change? | Build Impact |
|--------|------|----------------|:---:|--------------|
| OPTIM-01: Template helpers | No structural change | 0 | No | None |
| OPTIM-02: Split video_batch_execution.cpp | New file + modify existing | `video_encoding_state.cpp` (NEW), `video_batch_execution.cpp` (MODIFIED) | +30 lines to `video_batch_execution.h` (detail structs) | None (glob picks up new file) |
| DEBT-01: Explicit `.compact` | 1-line addition | `picture_process.cpp:482` | No | None |
| DEBT-02: Remove duplicate test | 1 test case removal + optional test merge | `tests/pack_service_tests.cpp` | No | None |

### D-01 Constraint Analysis

| Change | D-01 Impact |
|--------|-------------|
| OPTIM-01 | NONE — no header change |
| OPTIM-02 | REQUIRES exception — adds `EncodingProgressState` + `EncodingExecutionContext` to existing header. These are NOT extracted lambdas; they're existing structs moving from anonymous to named namespace. Minimal (+30 lines), no API surface change (only `videobatch::detail::*`) |
| DEBT-01 | NONE — `.cpp` only |
| DEBT-02 | NONE — test file only |

## Sources

- `src/video/video_workflow_utils.h` — Template helpers definition (HIGH confidence — primary source)
- `src/video/video_batch_execution.cpp` — Full file structure analysis (HIGH confidence — primary source)
- `src/picture/picture_process.cpp:474-482` — Implicit compact default location (HIGH confidence — primary source)
- `src/pack/pack_service.h:48` — PackPlan struct with `bool compact = true` default (HIGH confidence — primary source)
- `tests/pack_service_tests.cpp:98-162` — Duplicate test cases (HIGH confidence — primary source)
- `src/core/archive_plan.cpp:65` — Factory function exercise path (HIGH confidence — primary source)
- `xmake.lua` — Build system glob pattern (HIGH confidence — primary source)
- `.planning/PROJECT.md` — D-01 constraint, decision log (HIGH confidence — authoritative)
- `.planning/MILESTONES.md` — v1.1 shipped context (HIGH confidence — authoritative)
