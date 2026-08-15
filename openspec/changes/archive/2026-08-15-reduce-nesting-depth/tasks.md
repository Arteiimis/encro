# Tasks — Reduce Nesting Depth

Phases follow design.md Migration Plan; each phase is one atomic commit (tests + code + this file's checkboxes), independently revertable. TDD: failing test first, then the code.

## 1. P0 — Waitable stop event

- [x] 1.1 Add failing tests for the event primitive: signaled on `requestStop()`/console-handler path, cleared by `reset()`, and `ScopedStopSignalReset` restores it (extend the existing stop-signal test fixture)
- [x] 1.2 Implement `stopEventHandle()` in `src/infra/stop_signal.*`: Windows manual-reset event signaled in handler/`requestStop()`, cleared in `reset()`; POSIX self-pipe with async-signal-safe `write()`
- [x] 1.3 Force-exit watchdog: kept the 50 ms `sleep_for` — a wake-on-stop wait hot-spins on the manual-reset event with no benefit for a never-exiting backstop; watchdog test hardened with reset-before-handler-restore ordering
- [x] 1.4 Rewrite encoding monitor loop (`src/video/video_encoding_state.cpp`) to `WaitForSingleObject(event, 20ms)` tick; add a test that the monitor exits promptly on stop
- [x] 1.5 Rewrite pack spinner loop (`src/pack/pack_service.cpp`) to event-timed 120 ms wait; verify spinner stop promptness in pack tests

## 2. P1 — `exec2` coroutine rewrite (sync facade unchanged)

- [x] 2.1 Add a stop-race test where stop arrives mid-run and output arrives after termination, asserting 130 + partial output + no hang (extends existing `[subprocess-exec]` stop scenarios)
- [x] 2.2 Rewrite `exec2Impl` as `asio::awaitable` with the `(async_wait || stopEvent || graceTimer)` race, async pipe read loop, and detach-on-grace-expiry; keep the four public `exec2` overloads unchanged. Deviation: exit/stop waits are 20 ms timer polls, not `async_wait`/event awaits — asio `object_handle` waits and `bp::v2 async_wait` are not reliably cancellable on Windows, and a cancelled-but-pending op would pin `ctx.run()` forever (contract: stop latency ≤ 20 ms, as in the legacy poll loop; monitor/spinner keep the instant event wake)
- [x] 2.3 Run the full existing `subprocess-exec` suite unchanged — green without edits is the acceptance criterion
- [x] 2.4 Verify build modes: `coverage` full suite green (no coroutine-induced regressions); `releasedbg` ASan verification blocked by environment — package reinstall under `-fsanitize=address` fails at libzip's cmake ABI check (flags propagate into packages). Coroutine/stacktrace caveat recorded in the exec2 comment block

## 3. P2 — Mechanical flattening (batched per file)

- [x] 3.1 `job_state.cpp`: add `jsonOrNull` helper; replace the 11 ternary alignment pairs in `toJson` (behavior-identical output verified by existing job-state round-trip tests)
- [x] 3.2 `video_workflow_utils.h` + call sites: convert 15 `withActionJobState` chains to `maybeJobState()` pointer style; delete the template if no callers remain
- [x] 3.3 `video_batch_execution.cpp`: extract `runEncodingTask` outcome collection and finalization into helpers; flatten lock-copy blocks with `EncodingState` snapshot accessors
- [x] 3.4 `video_encoding_state.cpp`: extract `renderStalled`/`renderProgress`/`stateFinished`; apply guard clauses (monitor tick body now ≤ 3 levels)
- [x] 3.5 `cmd.cpp`: extract help column-width computation from the for-for-if-if ladder
- [x] 3.6 Sweep remaining lock-copy sites (47 total) for snapshot/accessor extraction where it reduces depth — the deep multi-field blocks were extracted in 3.3/3.4; remaining `scoped_lock` sites are accessor-style single locks at depth ≤ 2, left as-is

## 4. Verification gates

- [x] 4.1 `xmake build tests && xmake run tests` — full unit suite green in all build modes
- [x] 4.2 `xmake test-report` — clean summary
- [x] 4.3 e2e suite green (`xmake build e2e_tests && xmake run e2e_tests`, fake tool env)
- [x] 4.4 `xmake coverage` sanity (no coroutine-induced coverage regressions beyond documented state-machine noise)
- [x] 4.5 Run the `code-review` skill (Standards + Spec axes) and resolve findings — fixed: POSIX self-pipe now created in `installHandler()` (handler stays async-signal-safe), stop-after-exit 20 ms window (terminateOnStop re-checks `running()`, reports real exit code), member trailing underscores, include order, `terminateOnStop` dedup, `holds_alternative` replaces `outcome.index()` coupling, new test uses `ScopedStopSignalReset`; accepted as recorded: per-phase commits per design Migration Plan, maybeJobState pointer-style repetition (deliberate replacement of the template), design.md reconciled with implemented D1/D2/D3
