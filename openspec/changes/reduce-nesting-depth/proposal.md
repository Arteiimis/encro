# Reduce Nesting Depth

## Why

The codebase's readability pain is concentrated in a handful of async glue files, where deep physical nesting (measured max brace depth: 9 in the pack spinner, 8 in the encoding monitor / task executor / encoding task, 7 in `exec2`) comes from a few recurring patterns: thread-lambda-plus-poll loops, lock-copy blocks, callback-chain helpers, and if-ladders. These are exactly the files touched during debugging (subprocess handling, progress monitoring, batch orchestration). Most of the depth is removable with cheap, behavior-preserving refactors; C++20 coroutines are the right tool for exactly one spot — `exec2`'s hand-rolled async lifetime management.

## What Changes

- **Waitable stop event** in `stopsignal`: an OS waitable object signaled on stop request (Windows event handle; POSIX self-pipe), alongside the existing atomic flag (which keeps serving the 24 cheap `isStopRequested()` check sites). `reset()` clears it. Waiting code becomes a single `WaitForSingleObject(event, period)` call; stop response latency drops from ≤20 ms poll granularity to immediate.
- **`exec2` coroutine rewrite** (sync-over-async): internals become an `asio::awaitable` driven synchronously on the caller's thread — the public sync facade and the entire `ExecResult` contract stay unchanged. The reader thread, heap promise/future, `callbackEnabled` atomic, and the detach dance collapse into a straight-line coroutine with a `co_await (exit || stopEvent || graceTimeout)` race. All `subprocess-exec` spec requirements remain the behavioral contract.
- **Poll loops → event waits** in the encoding monitor (20 ms), pack spinner (120 ms), and force-exit watchdog (50 ms), with tick bodies flattened via guard clauses and extracted helpers.
- **Mechanical flattening** (no behavior change):
  - lock-copy blocks (`{ auto lock = scoped_lock{mtx}; x = s.x; ... }`, 47 sites) → snapshot/accessor helpers
  - `withActionJobState` callback chains (15 sites) → direct `maybeJobState()` pointer style
  - ternary continuation alignment (e.g. `job_state.cpp` `toJson`) → `jsonOrNull` helper
  - `cmd.cpp` help column-width computation extracted from the for-for-if-if ladder
  - `runEncodingTask` outcome collection / finalization extracted

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None. This is a pure refactor: observable behavior (exit codes, captured output, line callbacks, cancellation semantics, progress rendering) is preserved. The existing `subprocess-exec` spec pins the contract that the `exec2` rewrite must keep. `skip_specs: true` is declared in the change's `.openspec.yaml`.

## Impact

- `src/infra/stop_signal.*` — waitable event primitive (additive; flag-based checks unchanged)
- `src/utils/utils.*` — `exec2` internal coroutine rewrite; facade unchanged
- `src/video/video_encoding_state.cpp` — monitor loop flattening
- `src/pack/pack_service.cpp` — spinner loop flattening
- `src/core/job_state.cpp` — `jsonOrNull` extraction
- `src/cmd/cmd.cpp` — help renderer extraction
- `src/video/video_batch_execution.cpp`, `src/video/video_workflow_utils.h` — callback-chain flattening
- Dependencies: none added (boost::asio already present; `awaitable_operators` used for the stop race)
- Risks: coroutine frames vs crash-stacktrace forensics; ASan/coverage interplay with coroutines; `ScopedStopSignalReset` test hook must also reset the event — addressed in design.md
