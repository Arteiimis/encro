## Why

`runTasks` reports each task's result as two parallel arrays (`src/core/task_executor.h:36-39`) whose slots are only meaningful together, because a slot the stop signal skipped keeps a *default-constructed* `eh::Result<void>` — and a default-constructed `std::expected` **is a success** (`src/core/task_executor.cpp:73`). The pairing rule therefore has to be re-derived at every consumer, and today it is re-derived five different ways across nine call sites: `attempted[i] == 0 → continue` (`video_batch_execution.cpp:444`, `pack_service.cpp:310`), a bounds-checked read (`encode_probe.cpp:707`), `canceled && attemptedCount < n` (`pack_service.cpp:305`), a bare `canceled` gate (`organize/pipeline.cpp:189` returns `!canceled`; `picture_compress.cpp:315` returns the partial phase), and three sites that discard the result entirely (`preview_process.cpp:477`, `video_info.cpp:216,261`).

The rule lives in a caller comment — *"A task that threw was caught by the executor with its packResults entry left default-constructed (success) — never report such a run as success"* (`pack_service.cpp:312-313`) — and costs pack a second check channel (`:311-316`) that no type distinguishes from a plain success.

Why now: nine call sites, and this shape is what the batch-progress candidate in the same module would have to build on.

## What Changes

- **One outcome per slot**: `taskexec::TaskOutcome{TaskState state; std::string error;}`, `enum class TaskState { Skipped, Succeeded, Failed }`, and `TaskRunResult{std::vector<TaskOutcome> outcomes; std::size_t attemptedCount; bool canceled;}` plus a derived `skippedCount()`. A skipped slot is a `TaskState`, not a success value, so the trap is unrepresentable rather than documented.
- **Nothing is lost by flattening**: `eh::Result<Ty>` is `std::expected<Ty, std::string>` (`src/core/error_handle.h:9`), and callers only ever read `.has_value()` / `.error()`. `runOneTask` keeps returning `eh::Result<void>` internally and the executor maps it into the outcome.
- **`canceled` keeps its meaning** — the stop signal at return time, unchanged (`task_executor.cpp:117`); `skippedCount()` is the derived "did the stop actually cut work short" that `pack_service.cpp:305` computes by hand today. Redefining `canceled` as "work was cut short" is explicitly **not** done: it would flip a late Ctrl-C from canceled to succeeded (`organize/pipeline.cpp:189`, `picture_compress.cpp:315`), and cancellation semantics are pinned by specs (`job-state-resume-matching`: a canceled run that already started keeps its state and cache).
- **Pack's double check collapses to one**: with the executor's own outcome carrying the thrown-exception case, `pack_service.cpp:311-316` becomes a single `state == Failed` check and the "left default-constructed (success)" comment goes away. The behavior it protects stays pinned by `tests/pack_service_tests.cpp:686` ("packGroups reports failure when a group task throws").
- **No aggregation helper**: the nine callers map their own keys (path, group index, label), so a shared `failures()` view would buy one line each and cost a new interface plus an index-semantics caveat. The three-state outcome removes the trap; the loops become plain filters.
- **Tests**: of the seven `[task-executor]` cases in `tests/task_executor_tests.cpp`, the six that read the run result move to the new shape (the `resolveWorkerCount` clamp case is untouched), and one new named case stages a stop mid-run and asserts that a skipped slot reports `Skipped` — not success — and that `skippedCount()` counts it. No stop-mid-run case exists today, which is exactly how the trap survived.

No behavior change in any command: the same tasks run, the same failures are reported with the same messages, and the same slots are skipped on cancellation.

## Capabilities

### New Capabilities

None — this reshapes an existing result type; it adds no observable behavior.

### Modified Capabilities

None — the task-executor result shape is internal; `job-state-resume-matching` and the progress specs describe cancellation and progress behavior, neither of which changes. `skip_specs: true` is set in `.openspec.yaml` per the precedent of `refactor-long-param-lists` / `remove-immer-simplify-locks` / `reduce-over-engineering`.

## Impact

- `src/core/task_executor.{h,cpp}` — `TaskState`, `TaskOutcome`, the reshaped `TaskRunResult`, `skippedCount()`; the worker loop fills one outcome per slot
- Nine call sites: `src/organize/pipeline.cpp:181`, `src/video/encode_probe.cpp:406,849`, `src/video/video_batch_execution.cpp:630`, `src/video/video_info.cpp:216,261`, `src/picture/picture_compress.cpp:308`, `src/preview/preview_process.cpp:477`, `src/pack/pack_service.cpp:298` — mechanical, except `pack_service.cpp:305,311-316` where two checks become one
- `tests/task_executor_tests.cpp` — six cases updated, one added; no other test reads `results`/`attempted`
- No new files, no new dependencies, no CLI or file-format surface touched.

**Explicitly out of scope:** moving batch counting/rate/cursor into `runTasks` (the separate progress candidate over the same module), and redefining cancellation semantics.
