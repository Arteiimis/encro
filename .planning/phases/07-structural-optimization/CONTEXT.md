# Phase 7 Context: Structural Optimization

**Date:** 2026-04-28
**Source:** Discussion phase (gsd-discuss-phase 7)
**Status:** COMPLETE — Plan 7-1 (STRUCT-02) executed 2026-04-29

---

## Phase Overview

Split `src/video/video_batch_execution.cpp` (700 lines) into two compilation units to improve code organization. The relocation of template helpers (STRUCT-01) was discussed and determined unnecessary — templates in `video_workflow_utils.h` are already the correct C++ pattern (see Decision 2).

---

## Decisions Resolved

### D-01: Execution Order
**Verdict:** STRUCT-02 first (split video_batch_execution.cpp), STRUCT-01 cancelled.

*Rationale:* The split files don't need any relocated header; they reference `video_workflow_utils.h` which stays in place. The FEATURES.md recommendation to do STRUCT-01 first was based on a later relocation being unnecessary — since we're NOT relocating, order is moot.

### D-02: Relocation Scope — CANCELLED
**Verdict:** Keep templates (`withJobState`, `withActionJobState`, `maybeJobState`) in `video_workflow_utils.h`. No new `core/job_state_utils.h` header.

*Rationale:* ARCHITECTURE.md research confirms templates in a shared header under the video subsystem is already the correct C++ pattern. The templates are zero-cost, well-constrained, and the current namespace (`videoworkflow`) is accurate for their primary use. Creating a new `core/` header would be cosmetic relocation with no structural benefit.

This cancels STRUCT-01 — Phase 7 goes from 2 plans to 1 plan.

### D-03: File Content Boundaries
**Verdict:** The proposed boundary (VIDEO_ENCODING_STATE ~420 lines + modified VIDEO_BATCH_EXECUTION ~410 lines) is correct.

#### `src/video/video_encoding_state.cpp` (NEW, ~420 lines)
Contents extracted from `video_batch_execution.cpp`:
- Helper functions: `truncateForProgressLabel` (72-75), `makeSlotLabel` (77-79), `getStateLabel` (81-83)
- `EncodingProgressState` struct + methods (87-182)
- `EncodingExecutionContext` struct + methods (184-329)
- Progress I/O: `tryReadProgressData` (331), `getEncodingProgress` (336), `reportEncodingStatus` (368), `createEncodingState` (388)
- Monitor: `monitorEncodingProgress` (408-519), `startEncodingMonitor` (518-521)
- Small helpers: `noteStopRequest` (35-37), `markRunningNoProgress` (40-50), `finalizeEncodeResult` (53-69)

#### `src/video/video_batch_execution.cpp` (MODIFIED, ~410 lines)
Retains:
- Task execution: `runEncodingTask` (522-630)
- Entry points: `runEncodingWithoutProgress` (632-687), `videobatch::runEncodingTasks` (688-700)
- Remaining `#include` directives and `using` declarations (1-31)

### D-04: Header Exception Scope
**Verdict:** Add full struct definitions of `EncodingProgressState` and `EncodingExecutionContext` to `video_batch_execution.h` under `videobatch::detail` namespace (~140 lines added).

*Rationale:* `video_batch_execution.cpp` accesses these structs by value and by reference when calling `runEncodingTask` and `runEncodingWithoutProgress`. Forward declarations are insufficient for value semantics and reference parameter types. The struct definitions must be visible in both compilation units. This is the "narrow D-01 exception" acknowledged in the ROADMAP (~30 lines originally estimated; actual scope is ~140 lines including methods).

---

## Requirements (Refined)

| ID | Requirement | Status |
|----|-------------|--------|
| STRUCT-02 | Split `video_batch_execution.cpp` into 2 compilation units | Pending |
| STRUCT-01 | Relocate template helpers to `core/` | **CANCELLED** — unnecessary; templates already correctly placed |

---

## Success Criteria

1. `video_encoding_state.cpp` compiles independently as part of the build
2. `video_batch_execution.cpp` compiles independently (no anonymous-namespace cross-references)
3. `EncodingProgressState` + `EncodingExecutionContext` accessible via `videobatch::detail` in header
4. All 909 assertions pass across 215 test cases
5. 4 E2E flows produce identical binary output
6. `xmake build` succeeds with both `.cpp` files

---

## Implementation Constraints

| Constraint | Source | Detail |
|-----------|--------|--------|
| Zero behavioral change | D-01 (CONTEXT.md v1.0) | Byte-identical output for all E2E flows |
| No header modifications except as specified | D-01 (CONTEXT.md v1.0) | `video_batch_execution.h` gets ~140 line addition for `videobatch::detail`; no other headers touched |
| No test modifications | D-02 (CONTEXT.md v1.0) | Existing 909 assertions must pass unchanged |
| Use existing patterns | Project convention | Follow codebase conventions for namespacing, function signatures, include style |
| xmake wildcard build | Build system | `add_files("src/**.cpp")` — new `.cpp` file is auto-included, no build config changes needed |
| No breaking public API | Structural constraint | `videobatch::runEncodingTasks` signature unchanged |

---

## Plan Structure

Single plan for Phase 7 (STRUCT-02):

**Plan 7-1: STRUCT-02 — Split video_batch_execution.cpp**
1. Add `videobatch::detail` namespace to `video_batch_execution.h` with `EncodingProgressState` + `EncodingExecutionContext` struct definitions (~140 lines)
2. Create `src/video/video_encoding_state.cpp` with extracted state, monitor, progress I/O, and helper functions (~420 lines)
3. Strip extracted content from `video_batch_execution.cpp`, retain task execution + entry points (~410 lines)
4. Add `video_encoding_state.cpp` includes: `video_batch_execution.h`, system headers, app headers
5. Verify: build, test suite (909 assertions), E2E binary comparison

---

## Key Files

| File | Role |
|------|------|
| `src/video/video_batch_execution.h` | Public API + `videobatch::detail` struct definitions (D-04) |
| `src/video/video_batch_execution.cpp` | Task execution + entry points (MODIFIED, ~410 lines) |
| `src/video/video_encoding_state.cpp` | State management + monitor + progress I/O (NEW, ~420 lines) |
| `src/video/video_workflow_utils.h` | Template helpers — `withJobState`, `withActionJobState`, `maybeJobState` (UNCHANGED, D-02) |
| `.planning/research/FEATURES.md` | Full analysis of STRUCT-02 scope and boundaries |
| `.planning/research/ARCHITECTURE.md` | Template helper analysis (Q1: no relocation needed) |

---

## Environment

| Component | Version |
|-----------|---------|
| OS | Windows 11 |
| Compiler | clang-cl (C++26) |
| Build tool | xmake |
| FFmpeg | available at test time |
| Test framework | Catch2 |
| Assertions | 909 (215 test cases) |

---

## Discussion Trace

- **Q1 (ARCHITECTURE.md):** Should template helpers relocate to `core/`?
  - Research: Templates in a shared header is already correct C++ pattern. No relocation needed.
  - Verdict: Keep in `video_workflow_utils.h` → STRUCT-01 cancelled.

- **Decision 1 (discuss-phase agent):** STRUCT-02 first or STRUCT-01 first?
  - Verdict: STRUCT-02 first. STRUCT-01 is cancelled anyway; no dependency.

- **Decision 2 (discuss-phase agent):** Content boundaries for the split?
  - Verdict: State management + monitor + progress I/O → new file (~420 lines). Task execution + entry points → remain in original (~410 lines).

- **Decision 3 (discuss-phase agent):** Header exposure for struct definitions?
  - Verdict: Full definitions in `video_batch_execution.h` under `videobatch::detail`. Required for value semantics and reference parameters across compilation units.
