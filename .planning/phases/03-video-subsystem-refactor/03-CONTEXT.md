# Phase 3: Video Subsystem Refactor - Context

**Gathered:** 2026-04-27
**Status:** Ready for planning

<domain>
## Phase Boundary

Eliminate deeply nested lambdas (3+ levels) in `src/video/video_batch_execution.cpp` by extracting them to named functions. No behavioral changes — all existing test assertions must pass unchanged. The file is currently 765 lines, nearly all in an anonymous namespace. Target: maximum lambda nesting depth ≤ 2 levels.

</domain>

<decisions>
## Implementation Decisions

### Extraction destination
- **D-01:** Extracted functions live as **free functions in the anonymous namespace** in `video_batch_execution.cpp`. Matches existing pattern (`noteStopRequest`, `makeSlotLabel`, `getStateLabel`). No new files or detail headers created.

### Captured variable handling
- **D-02:** Extracted functions receive captured variables as **individual typed parameters** (not context structs). E.g., `executionCtx&`, `vidState&`, `std::string_view fileLabel` — consistent with codebase style of free functions with explicit parameter types.

### withActionJobState/withJobState nesting
- **D-03:** **Leave 2-level `withActionJobState`/`withJobState` lambdas as-is** (their bodies are 4-8 lines of readable store operations). Extract only the **3+ level cases**.
- **D-04:** **Extract repeated `withActionJobState` patterns in `runEncodingWithoutProgress`** (lines 614-636: `markRunning` + `markSucceeded`/`markFailed` pairs) to named helper functions, following the `noteStopRequest` precedent at line 35.

### Naming convention
- **D-05:** Extracted functions use **descriptive camelCase** matching the codebase style: e.g., `reportEncodingStatus`, `finalizeEncodeResult`, `reportEncodingProgress`, `markRunningAndSucceed`, `markRunningAndFail`.

### Granularity
- **D-06:** Target **only the clear 3+ level cases** (~4-5 extracted functions):
  1. The `encodeVideo` status callback (line 496-509) — deepest nesting at 3 levels deep (inside `runEncodingTask` → inside `encodeVideo`)
  2. The `runEncodingWithoutProgress` repeated `withActionJobState` patterns (lines 614-636)
  3. The `startEncodingMonitor` jthread body (lines 350-458) — 109 lines, 2+ levels

### the agent's Discretion
- Exact function signatures (parameter ordering, const qualification)
- Whether to extract the startEncodingMonitor body to one function or split into inner helpers
- Whether the task spec lambda (line 733, 3 lines wrapping runEncodingTask) stays or gets extracted
- Order of extracted functions in the file (before or after the structs that use them)

</decisions>

<specifics>
## Specific Ideas

- Existing `noteStopRequest` (line 35) is the template for how a `withJobState` lambda is extracted — follow that pattern for new extractions.
- `withActionJobState` and `withJobState` themselves are NOT to be refactored (they are shared helpers used across the codebase).
- Mutex locking patterns (`std::scoped_lock{vidState->mtx}`) must be preserved exactly as-is in extracted functions.

</specifics>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Primary refactoring target
- `src/video/video_batch_execution.cpp` — Full 765-line file. Key lines: 35-38 (`noteStopRequest` template), 349-459 (`startEncodingMonitor`), 461-580 (`runEncodingTask` with encodeVideo callback at 496-509), 582-645 (`runEncodingWithoutProgress`), 726-739 (task spec lambda), 689-765 (`runEncodingTasks` public entry).

### Header
- `src/video/video_batch_execution.h` — Public API: `videobatch::runEncodingTasks()` signature, `ActionIdMap`, `EncodeResultsMap`.

### Job state helpers (used by nested lambdas)
- `src/video/video_workflow_utils.h` — `withActionJobState`, `withJobState`, `lookupPlannedOutputFile` signatures.

### Encoding context types
- `src/core/app_context.h` — `EncodingState`, `EncodingStatePtr`, `AppConfig`, `RuntimeContext` structs.
- `src/core/job_state.h` — `jobstate::Store` interface (markProgress, markRunning, markSucceeded, markFailed).
- `src/core/task_executor.h` — `TaskSpec`, `TaskPlan`, `TaskContext`, `runTasks()`.

### Codebase conventions
- `.planning/codebase/CONVENTIONS.md` — Naming (camelCase free functions, trailing return types), formatting (clang-format), lambda style, anonymous namespace pattern.
- `.planning/codebase/ARCHITECTURE.md` §Anti-Patterns — Anonymous namespace anti-pattern note (relevant context for the decision to keep extracted functions there).
- `.planning/codebase/TESTING.md` — Test run commands (`xmake run tests`), test file location (`tests/video/`), assertion patterns.

### Requirements
- `.planning/REQUIREMENTS.md` — REF-01: Extract deeply nested lambdas (3+ levels) in video_batch_execution.cpp. REF-05: All 876 assertions pass. REF-06: No behavioral changes.
- `.planning/ROADMAP.md` §Phase 3 — Success criteria: max depth ≤ 2, compile without errors, identical output, all video tests pass.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **`noteStopRequest()`** (line 35-38): Extracted `withJobState` lambda → template for similar extractions.
- **`EncodingExecutionContext`** (line 148-290): Struct providing helper methods (`barEncodingStart`, `barEncodingStatus`, `barIdle`, `updateOverall`, `finalizeState`). Extracted functions will call these methods.
- **`EncodingProgressState`** (line 55-146): Progress bar state with `createOverallBar`, `makeSlotBars`. Not modified by this phase.

### Established Patterns
- **Trailing return type** (`auto fn() -> ReturnType`): All new extracted functions must follow this convention.
- **`auto const` for immutable locals**: Use `auto const` instead of explicit type for local variables.
- **`std::format` for string construction**: Use `std::format()` not string concatenation.
- **Anonymous namespace `}  // namespace` comment**: Close with the trailing comment convention.

### Integration Points
- **`runEncodingTask()`** (line 461): The extracted `encodeVideo` callback replaces the inline lambda — `encodeVideo()` accepts `std::function<void(std::string)>` or similar callable.
- **`runEncodingWithoutProgress()`** (line 582): Extracted helpers replace the inline `withActionJobState` lambdas — same `jobstate::Store` API calls.
- **`startEncodingMonitor()`** (line 349): The jthread lambda body moves to a named function — `std::jthread` still constructed with it.

</code_context>

<deferred>
## Deferred Ideas

- Refactoring `withActionJobState`/`withJobState` themselves to a non-lambda pattern — out of scope (shared across codebase, would affect phases 4-5)
- Extracting functions to a `video_batch_execution_detail.h` — rejected in favor of anonymous namespace
- Reducing the overall file size (765 lines) through structural refactoring — this phase only targets lambda nesting

</deferred>

---

*Phase: 03-video-subsystem-refactor*
*Context gathered: 2026-04-27*
