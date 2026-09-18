## Context

See `proposal.md` — Why for the pain. The state that shapes the approach:

- The executor's result (`src/core/task_executor.h:35-40`) is `results` + `attempted` + `attemptedCount` + `canceled`; the worker loop marks `attempted[taskIndex] = 1` before running and stores the returned `eh::Result<void>` (`task_executor.cpp:100-107`), so a slot no worker ever reached keeps the default-constructed `std::expected` — a success. `canceled` is `stopsignal::isStopRequested()` read after the pool joins (`:117`).
- `runOneTask` (`task_executor.cpp:29-63`) already converts "no runner", synchronous exceptions and unknown exceptions into `eh::Result` errors with the `task_id`/`input` log attributes, so the executor is the one place that knows a task threw.
- Consumers, by site: `video_batch_execution.cpp:437-453` (`collectEncodingResults`: skip `!attempted`, record `has_value()` per input, collect reasons) and `:645` (`attemptedCount` into a summary); `pack_service.cpp:305` (`canceled && attemptedCount < groups`) and `:309-317` (skip `!attempted`, then check **both** the task-body channel `packResults[index]` and the executor's `results[index]`); `encode_probe.cpp:707` (bounds-checked `attempted[taskIndex]`); `video_info.cpp:216,261` and `preview_process.cpp:477` (discard); `organize/pipeline.cpp:189` (`!canceled`); `picture_compress.cpp:315` (`canceled` → discard the phase's results).
- `eh::Result<Ty>` is `std::expected<Ty, std::string>` (`error_handle.h:9`), and no consumer inspects an error kind — they format or propagate the string.
- Tests: `tests/task_executor_tests.cpp` has seven cases (`resolveWorkerCount` clamp, concurrency, "preserves task failures", "records thrown exceptions", and three attribute cases); six read the run result (the clamp case is untouched), and none requests a stop mid-run, so the skipped-slot path has no coverage. `tests/pack_service_tests.cpp:686` already pins the throwing-task contract at the pack level.
- `tests/task_executor_tests.cpp:80-90` reads `result.attempted` / `result.results` directly; no test outside this file touches them.

## Goals / Non-Goals

**Goals:**

- Make "skipped" a value that cannot be mistaken for success, so the rule stops living in a caller comment.
- Keep the aggregate facts consumers actually use (how many ran, was the user's stop in effect, which slot failed and why) without making them re-derive any of it.
- Collapse pack's two-channel failure check into the one check the executor can guarantee.

**Non-Goals:**

- **Redefining `canceled`.** It stays the raw stop signal; `skippedCount()` is the derived view. Changing it would flip late-Ctrl-C runs from canceled to succeeded (`organize/pipeline.cpp:189`, `picture_compress.cpp:315`), and cancellation semantics are spec'd (`job-state-resume-matching:73`).
- **A shared failure-aggregation helper** — the nine callers key failures by path, group index or label; the three-state outcome already removes the trap and the loops become filters.
- **Touching `TaskSpec`, `TaskPlan`, `TaskContext`** (ids, labels, input correlation, progress, `hideCursor`) or the cursor/`hideCursor` behavior.
- **Moving batch progress into `runTasks`** — that is the separate progress candidate over this same module.
- **Changing which tasks run, in what order, or with what concurrency** — `resolveWorkerCount` and the pool semantics stay as they are.

## Decisions

**D1 — Three states per slot, no default success.**

```cpp
namespace taskexec {

enum class TaskState { Skipped, Succeeded, Failed };

struct TaskOutcome {
  TaskState state = TaskState::Skipped;  // default = not attempted, never success
  std::string error;                     // set only for Failed
};

struct TaskRunResult {
  std::vector<TaskOutcome> outcomes;
  std::size_t attemptedCount = 0;
  bool canceled = false;
  auto skippedCount() const -> std::size_t { return outcomes.size() - attemptedCount; }
};
}
```

`runOneTask` keeps returning `eh::Result<void>`; the worker loop maps `has_value()` → `Succeeded`/`Failed` and stores the error string. A slot the stop signal skipped is `Skipped` — there is no success value to misread, which is the whole point.
*Alternatives:* `{bool attempted; eh::Result<void> result;}` (the review's first shape) bundles the arrays but still ships a default success for skipped slots, so the trap survives; `Result<SkippedTag>` would put a fake error in the failure path and make skipped slots look like failures in failure lists.

**D2 — `canceled` keeps its meaning; `skippedCount()` is the derived view.**
`canceled` = "the stop signal was in effect when the run returned" (unchanged, `task_executor.cpp:117`). `skippedCount()` replaces the hand-rolled `attemptedCount < n` comparison at `pack_service.cpp:305` and gives the new test something to assert. `attemptedCount` stays because `video_batch_execution.cpp:645` reports it.
*Alternative:* make `canceled` mean "work was actually cut short" (`skippedCount() > 0`) — more useful at two call sites, but it silently changes both of their decisions and contradicts the spec'd cancellation behavior, so it belongs to its own change if it is wanted at all.

**D3 — No failure-aggregation helper.**
`video_batch_execution.cpp:444` and `pack_service.cpp:310` each own the index → key mapping (`vids[i]`, `packResults[i]`), so a shared `failures()` view would save one line per site while introducing an interface whose index semantics need documenting. After D1 both loops are plain filters:
```cpp
for (auto index = std::size_t{0}; index < vids.size(); ++index) {
  if (result.outcomes[index].state == taskexec::TaskState::Skipped) { continue; }
  ...
}
```

**D4 — Pack's two channels become one check.**
The executor's outcome is authoritative for "did this task fail (including throwing)"; `packResults[index]` stays the task body's own payload channel. So `pack_service.cpp:311-316` becomes a single `state == Failed` check, and the comment explaining the default-constructed success is deleted with it. The behavior it protected is asserted by `tests/pack_service_tests.cpp:686`, which must stay green.

**D5 — Tests: six updated, one added.**
`tests/task_executor_tests.cpp`'s six cases move to `outcomes` (the concurrency case asserts six `Succeeded`; "preserves task failures" asserts `Succeeded/Failed/Succeeded` with the error text; the attribute cases read states only). The added case — **"a slot the stop signal skipped is not a success"** — starts a run with `maxConcurrency = 1` whose first task body records observable state and requests a stop through `stopsignal::requestStop()` (guarded by `testutils::ScopedStopSignalReset`, so the skipped slot is deterministic), and asserts: at least one `Skipped`, `skippedCount() > 0`, `canceled == true`, and no slot that was never attempted reports `Succeeded`. No stop-mid-run case exists today, which is how the trap survived; the new case is its named regression.

## Risks / Trade-offs

- [A consumer relied on the fake success of a skipped slot] → Audited all nine sites (Context): every one either skips `!attempted` explicitly, returns early on `canceled`, or discards the result, so `Skipped` being distinct changes no decision.
- [Pack's collapse weakens the throwing-task guard] → The guard moves from a second channel read to the executor's own outcome, which is the channel that handles the throw; `pack_service_tests.cpp:686` stays green as the gate.
- [The new stop-mid-run case is timing-sensitive] → It synchronizes on observable state (poll the stop flag effect, or a gate the task body signals) rather than sleeping; the repo's sync convention forbids fixed sleeps.

## Migration Plan

None: internal only, no persisted format, no CLI surface, no spec-level behavior change. Rollback is a revert of the single refactor commit.
