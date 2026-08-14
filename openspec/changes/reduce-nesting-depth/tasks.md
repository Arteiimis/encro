# Tasks — Reduce Nesting Depth

Phases follow design.md Migration Plan; each phase is one atomic commit (tests + code + this file's checkboxes), independently revertable. TDD: failing test first, then the code.

## 1. P0 — Waitable stop event

- [x] 1.1 Add failing tests for the event primitive: signaled on `requestStop()`/console-handler path, cleared by `reset()`, and `ScopedStopSignalReset` restores it (extend the existing stop-signal test fixture)
- [x] 1.2 Implement `stopEventHandle()` in `src/infra/stop_signal.*`: Windows manual-reset event signaled in handler/`requestStop()`, cleared in `reset()`; POSIX self-pipe with async-signal-safe `write()`
- [x] 1.3 Force-exit watchdog: kept the 50 ms `sleep_for` — a wake-on-stop wait hot-spins on the manual-reset event with no benefit for a never-exiting backstop; watchdog test hardened with reset-before-handler-restore ordering
- [x] 1.4 Rewrite encoding monitor loop (`src/video/video_encoding_state.cpp`) to `WaitForSingleObject(event, 20ms)` tick; add a test that the monitor exits promptly on stop
- [x] 1.5 Rewrite pack spinner loop (`src/pack/pack_service.cpp`) to event-timed 120 ms wait; verify spinner stop promptness in pack tests

## 2. P1 — `exec2` coroutine rewrite (sync facade unchanged)

- [ ] 2.1 Add a stop-race test where stop arrives mid-run and output arrives after termination, asserting 130 + partial output + no hang (extends existing `[subprocess-exec]` stop scenarios)
- [ ] 2.2 Rewrite `exec2Impl` as `asio::awaitable` with the `(async_wait || stopEvent || graceTimer)` race, async pipe read loop, and detach-on-grace-expiry; keep the four public `exec2` overloads unchanged
- [ ] 2.3 Run the full existing `subprocess-exec` suite unchanged — green without edits is the acceptance criterion
- [ ] 2.4 Verify in `releasedbg` (ASan) and `coverage` build modes; record coroutine/stacktrace caveat in the function comment

## 3. P2 — Mechanical flattening (batched per file)

- [ ] 3.1 `job_state.cpp`: add `jsonOrNull` helper; replace the 11 ternary alignment pairs in `toJson` (behavior-identical output verified by existing job-state round-trip tests)
- [ ] 3.2 `video_workflow_utils.h` + call sites: convert 15 `withActionJobState` chains to `maybeJobState()` pointer style; delete the template if no callers remain
- [ ] 3.3 `video_batch_execution.cpp`: extract `runEncodingTask` outcome collection and finalization into helpers; flatten lock-copy blocks with `EncodingState` snapshot accessors
- [ ] 3.4 `video_encoding_state.cpp`: extract `renderStalled`/tick helpers; apply guard clauses (target: monitor tick body ≤ 4 levels)
- [ ] 3.5 `cmd.cpp`: extract help column-width computation from the for-for-if-if ladder
- [ ] 3.6 Sweep remaining lock-copy sites (47 total) for snapshot/accessor extraction where it reduces depth

## 4. Verification gates

- [ ] 4.1 `xmake build tests && xmake run tests` — full unit suite green in all build modes
- [ ] 4.2 `xmake test-report` — clean summary
- [ ] 4.3 e2e suite green (`xmake build e2e_tests && xmake run e2e_tests`, fake tool env)
- [ ] 4.4 `xmake coverage` sanity (no coroutine-induced coverage regressions beyond documented state-machine noise)
- [ ] 4.5 Run the `code-review` skill (Standards + Spec axes) and resolve findings
