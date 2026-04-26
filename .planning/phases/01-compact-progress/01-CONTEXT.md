# Phase 1: Compact Progress Mode - Context

**Gathered:** 2026-04-26
**Status:** Ready for planning

<domain>
## Phase Boundary

Replace the multi-bar progress display (per-worker encoding bars + per-archive packing bars) with a default compact mode showing only overall progress. The compact mode is the new default; the old full-progress behavior is opt-in via `--full-progress`. Applies to all progress bar scenarios: video batch encoding, picture compression, pack-only, and encode+pack workflows. Single-file encoding retains the existing detailed progress bar.

</domain>

<decisions>
## Implementation Decisions

### Activation
- **D-01:** Compact progress is the **default** behavior. No flag needed to enable it.
- **D-02:** `--full-progress` CLI flag restores the old multi-bar behavior (per-worker slot bars during encoding, per-archive bars during packing).
- **D-03:** `--verbose-echo` still disables all progress bars entirely — it takes priority over both compact and full-progress modes. No error when combined with `--full-progress`.

### Encoding Progress (batch, compact mode)
- **D-04:** Single overall bar displaying "Overall: X/Y" with percentage (same style as existing overall bar in `EncodingProgressState::createOverallBar()`).
- **D-05:** Overall bar **always shown** (currently only appears when `totalTasks > workerCount`). Remove the guard condition.
- **D-06:** Per-worker slot bars (`slots.barIndexes`) are **not created** in compact mode. The `makeSlotBars()` logic is skipped.
- **D-07:** Single-file encoding (non-batch) keeps the existing detailed progress bar — no changes.

### Packing Progress (compact mode)
- **D-08:** Single overall bar displaying "Packing: X/Y" style, replacing per-archive progress bars.
- **D-09:** Progress based on archive count across all pack groups. The bar advances as each archive completes.
- **D-10:** Per-group progress bars are not created in compact mode. The `taskCtx.progress` approach in `packGroups()` is adjusted to use a shared overall bar.

### Scope
- **D-11:** Compact mode applies to all progress bar scenarios: video batch encoding, picture compression, pack-only mode, and post-encode packing.
- **D-12:** Picture compression and pack-only workflows follow the same compact pattern (single overall bar, no per-item bars).

### User Interaction
- **D-13:** All user prompts and summaries remain unchanged: confirmation prompt ("do you want to encode...?"), final encoding summary, packing summary messages.

### Flag Interactions
- **D-14:** `--full-progress` shows all worker/archive bars (old behavior).
- **D-15:** `--verbose-echo` disables all progress bars (existing, unchanged). When both `--full-progress` and `--verbose-echo` are set, verbose-echo wins.
- **D-16:** No `--compact-progress` flag — compact is the default.

### the agent's Discretion
- Exact percentage calculation formula for the packing overall bar (file count weighted vs. archive count only)
- Whether picture compression already uses overall bars or needs adaptation
- How `taskexec::TaskPlan` and `taskexec::TaskContext` are modified to support the overall-bar-only mode
- Internal refactoring approach (e.g., adding a `compact` parameter to `EncodingProgressState`, `PackPlan`, or `runEncodingTasks`)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Progress infrastructure
- `src/core/progress.h` — `ProgressContext`, `Tone`, `makeBar()`, `addBar()` API
- `src/core/progress.cpp` — Bar layout, postfix text fitting, color resolution

### Encoding progress (primary change target)
- `src/video/video_batch_execution.cpp:55-142` — `EncodingProgressState` struct: `createOverallBar()`, `makeSlotBars()`, bar lifecycle
- `src/video/video_batch_execution.cpp:451-570` — `runEncodingTask()`: per-worker bar updates via `barEncodingStart()`, `barIdle()`
- `src/video/video_batch_execution.h` — `runEncodingTasks()` signature

### Packing progress
- `src/pack/pack_service.cpp:147-213` — `packGroups()`: per-group task creation with progress bars
- `src/pack/pack_service.h:36-46` — `PackPlan` struct with `progressLabelForIndex`
- `src/pack/packer.h:46-59` — `packFilesToZip()` signatures accepting `ProgressContext&`

### Workflow orchestration
- `src/video/video_process.cpp:265-313` — `runScannedEncodingWorkflow()`: encode → pack sequence
- `src/video/video_process.cpp:358-431` — `packEncodedVideos()`: pack plan construction

### Task execution
- `src/core/task_executor.h` — `TaskSpec`, `TaskPlan`, `TaskContext` (progress context handling)
- `src/core/task_executor.cpp` — `runTasks()` implementation

### CLI / config
- `src/core/app_context.h:38-60` — `AppConfig` struct (flags to add)
- `src/cmd/cmd.cpp` — CLI argument definitions
- `src/cmd/config_builder.cpp` — Config construction from parsed flags

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **`EncodingProgressState::createOverallBar()`** (`video_batch_execution.cpp:114-128`): Already creates an overall bar with "Overall: X/Y" style. Currently gated by `totalTasks > workerCount`. Remove the gate for compact mode.
- **`EncodingExecutionContext::updateOverall()`** (`video_batch_execution.cpp:223-248`): Already calculates aggregated progress across all active slots. Can be reused as-is.
- **`progress::ProgressContext`** (`core/progress.h:31-46`): Thread-safe multi-bar manager. Already supports single-bar use case.
- **`progress::Tone`** (`core/progress.h:14-23`): Color tones for bar states (Overall, Active, Idle, Packing, etc.) — reusable.

### Established Patterns
- **Overall bar existence guard** (`totalTasks <= workerCount` → no overall bar): This is the key condition to change. Compact mode always creates the overall bar regardless of worker count.
- **Slot bar creation loop** (`makeSlotBars`, line 131-141): This loop should be skipped in compact mode. Bars are still needed for `runEncodingTask` which assigns `barIndex` per slot, but they should not be added to the progress display.
- **`taskCtx.progress`** in `TaskSpec::run`: Task execution passes a `ProgressContext&` per task. For compact packing, all pack tasks should share one bar instead of each getting its own.
- **`--verbose-echo` early return** (`video_batch_execution.cpp:674-677`): Existing pattern for progress bar suppression — compact mode can follow a similar early dispatch pattern.

### Integration Points
- **`AppConfig` struct** (`core/app_context.h:38`): New `bool fullProgress = false;` field needed.
- **`cmd::buildConfig()`** (`cmd/config_builder.cpp`): Wire `--full-progress` flag to `AppConfig::fullProgress`.
- **`cmd::cmd.cpp`**: Add `--full-progress` option definition to boost::program_options.
- **`runEncodingTasks()`** (`video_batch_execution.cpp:639`): Pass compact/full mode decision into `EncodingProgressState` constructor.
- **`packGroups()`** (`pack_service.cpp:147`): Adjust progress bar creation based on compact/full mode.
- **Picture processing paths**: Apply same compact/full logic to `picture_compress` and `pack_only` workflow.

</code_context>

<specifics>
## Specific Ideas

- User expects compact mode to feel like the existing overall bar but without per-worker noise — same "Overall: X/Y" format, same terminal color scheme.
- Packing bar should mirror encoding bar style ("Packing: X/Y") for visual consistency.
- Single-file encoding is intentionally excluded from compact mode — its detailed frame/time progress is useful when there's only one job.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope.

</deferred>

---

*Phase: 01-compact-progress*
*Context gathered: 2026-04-26*
