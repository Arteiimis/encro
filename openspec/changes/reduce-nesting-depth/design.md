# Design — Reduce Nesting Depth

## Context

Current state (see proposal.md for motivation):

- `stopsignal` is a plain `std::atomic<bool>` plus a Windows console handler and a 50 ms force-exit watchdog thread; 24 flag-check call sites exist.
- `exec2` spawns via boost.process v2, reads the merged pipe on a dedicated reader thread (heap promise/future because the thread may outlive the function on the stop-detach path), and the caller thread polls `process.running()` every 20 ms.
- The encoding monitor (20 ms), pack spinner (120 ms), and watchdog (50 ms) are `sleep_for` poll loops on threads.
- The project is Windows-primary (clang-cl, C++26), boost::asio already a dependency, tests are Catch2 with a strong suite (unit + e2e with a fake ffmpeg tool); `subprocess-exec` spec pins the `exec2` contract.
- Conventions: east const, trailing return types, minimal comments, TDD (test first, same commit).

## Goals / Non-Goals

**Goals:**

- Replace every sleep-poll loop that waits on stop-or-timeout with a single waitable-object wait.
- Rewrite `exec2` internals as one straight-line coroutine; the sync facade and the full `subprocess-exec` contract are preserved exactly.
- Flatten the measured nesting hotspots without changing behavior.

**Non-Goals:**

- No coroutines for monitor/spinner/watchdog (event wait is flatter and needs no runtime).
- No clang-format config changes (ternary alignment stays a formatter behavior; `jsonOrNull` removes the offending code instead).
- No parallel segment encoding, no full-async pipeline (方案 B). If that ever lands, these loops migrate to its runtime then.
- No changes to the 24 flag-based `isStopRequested()` sites.

## Decisions

### D1: Waitable event alongside the flag

`stopsignal` gains `stopEventHandle()` returning a native waitable object, kept in sync with the existing flag — the flag stays the source of truth for cheap checks.

- **Windows**: `CreateEvent` (manual-reset). `SetEvent` in the console handler, `requestStop()`, and cleared by `ResetEvent` in `reset()`. `SetEvent` is legal in a ctrl handler (it runs on its own thread). Manual-reset chosen because multiple waiters must all see the stop.
- **POSIX**: self-pipe; the signal handler only `write()`s one byte (async-signal-safe), the read end is the waitable fd.
- **Test hook**: `ScopedStopSignalReset` must also clear the event so tests that stop-and-reset don't leave waiters spuriously woken.
- **Watchdog**: keeps `sleep_for(50ms)` — a wake-on-stop wait would hot-spin (manual-reset event stays signaled) and buys nothing for a never-exiting backstop whose deadline check already has 50 ms resolution.
- *Alternative rejected*: replacing the flag entirely with the event — 24 sites don't block, they check; a flag read is cheaper and simpler there.

### D2: `exec2` as sync-over-async coroutine

A single `asio::awaitable<ExecResult>` running on a function-local `io_context` driven by the calling thread (`co_spawn(use_future)` + `run()`). Frame captures (`cmd`, `onLine`) are safe because the frame never outlives the call.

Shape (the readability payoff):

```cpp
auto [which, ec] = co_await (
  process.async_wait(asio::use_awaitable)
  || stopEvent.async_wait(asio::use_awaitable)
  || asio::steady_timer(ex, kTerminateGrace).async_wait(asio::use_awaitable)
);
```

- **Read loop**: `async_read_some` on the `readable_pipe` in the same coroutine; line splitting, CR stripping, and "no trailing newline" behavior identical to today (guarded by existing `subprocess-exec` tests).
- **Stop won the race**: `terminate()`, then `co_await (async_wait || grace timer)`; if grace expires → `detach()` (child may outlive us; handle released) and return `{130, partial, pid}`. No promise, no `callbackEnabled`, no thread join/detach dance.
- **`process.wait()` on natural exit**, full output returned — same ordering guarantees as today (read to EOF before returning).
- **Merge behavior**: stdout/stderr share one pipe write end exactly as today; `mergeStdErr=false` passes `err=nullptr`.
- **Facade**: `exec2(cmd[, onLine][, merge])` unchanged signatures.
- *Alternatives rejected*: full-async chain (contaminates the whole call stack with coroutine lifetime rules for no readability gain — the chain is already direct style); callback-only asio (the stop-race composition in callbacks is uglier than the code it replaces); keeping the reader thread (that thread + promise is precisely the nesting source being removed).
- **Known caveat**: boost.process v2 `async_wait` may internally use a wait thread on Windows; irrelevant here — the goal is code shape, not thread count.

### D3: Monitor / spinner / watchdog loops on event waits

```cpp
while (WaitForSingleObject(stopEvent, kTickMs) == WAIT_TIMEOUT) { tick(); }
```

Instant wake on stop; identical tick cadence. `tick()` bodies flattened with guard clauses (`if (!state) continue;`, `if (progress) continue;`) and extracted helpers (`renderStalled`, `snapshotState`, …). The loops keep their `std::jthread` RAII — no lifecycle change.

### D4: Mechanical flattening patterns

- **Lock-copy → snapshot**: `EncodingState` gains small snapshot accessors (e.g. fields copied out under `mtx` today) so call sites drop the `{ lock; copy }` scope.
- **`withActionJobState` → `maybeJobState()`**: 15 sites become `if (auto* store = maybeJobState(ctx); actionId) { store->markX(*actionId, …); }` — drops the lambda layer; helpers stay in `video_workflow_utils.h`.
- **`jsonOrNull`**: one helper for optional→`json::value(nullptr)`, replacing 11 aligned ternary pairs in `job_state.cpp`.
- **Extractions**: cmd.cpp help column-width computation; `runEncodingTask` outcome collection and finalization.

### D5: Verification strategy

- `subprocess-exec` test suite must pass **unchanged** — it is the behavioral contract for D2.
- New tests (TDD, before code): event signaled on `requestStop()` and cleared on `reset()`; stop-during-run returns 130 with partial output (already covered — re-verify); monitor/spinner exit promptly on stop.
- Full gates: `xmake test-report`, e2e suite, coverage sanity, `code-review` skill per project workflow.

## Risks / Trade-offs

- [Coroutine frames break crash-stacktrace forensics (`infra/stacktrace` shows bare `coroutine_handle::resume` frames)] → Mitigation: coroutine code is confined to one function; wrap the spawn in the existing crash context; document the limitation in the function.
- [ASan/coverage interplay with coroutines on clang-cl] → Mitigation: D2 ships last (P1) after P0/P2 flattening is green in all build modes; coverage mode check is part of the gate.
- [Async wait cancellation edge: stop arriving between `async_wait` completion and return] → Mitigation: the race composition makes this deterministic (exactly one branch wins); existing "stop after child exit" scenario must still pass.
- [POSIX self-pipe requires pollable fds and the project's POSIX CI is secondary] → Mitigation: keep POSIX implementation minimal; primary verification is Windows.
- [Refactor regressions in lock discipline] → Mitigation: TDD + full suite per phase; each flattening batch is behavior-preserving and reviewable per file.

## Migration Plan

Phased, each phase its own atomic commit (tests + code + tasks.md checkboxes):

1. **P0** — waitable event + monitor/spinner/watchdog on event waits (sync, low risk).
2. **P1** — `exec2` coroutine rewrite behind the unchanged facade.
3. **P2** — mechanical flattening batches by file (job_state, video_batch_execution/video_workflow_utils, cmd.cpp, remaining lock-copy sites).

Rollback: each phase is independently revertable (facade and contracts unchanged).
