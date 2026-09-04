# Design: stabilize-parallel-tests

## Context

`xmake test-parallel` runs 8 unit + 4 e2e Catch2 shard processes concurrently, each with an isolated TMP/TEMP root; shards are processes, test cases inside a shard are sequential, and verdicts are parsed from shard logs. Audit found no cross-shard shared writable state — every flake mechanism is a test-body timing assumption: fixed sleeps that must land inside an async window, fixed waits for async effects, and tight elapsed-time upper bounds. Windows process spawn under saturated load is slow and unbounded, which is what breaks them. One fix already landed this way (e86a35d: `ENCRO_FAKE_FFMPEG_GATE_FILE` + polling the invocation log in `video_batch_execution_tests.cpp`); this change generalizes it.

## Goals / Non-Goals

**Goals:**

- Every test's correctness depends only on observable state and ordering, never on wall-clock margins.
- Keep the current parallel architecture and wall-clock times (conversion usually makes tests faster: fixed delays disappear).
- Make the convention enforceable (meta-check) and diagnosable (per-test durations in shard logs).

**Non-Goals:**

- No changes to shard counts, TMP isolation, or log-based shard verdicts.
- No in-shard test-case parallelism (process-global state — stop signal, env vars, stdout, spdlog registry — makes that unsafe; process-per-shard stays).
- No retry/quarantine machinery: failures stay loud.
- No production behavior changes beyond a test-only clock hook.

## Decisions

### D1: State-based synchronization recipe is the canonical pattern

Convert "sleep N, then act" into "poll for proof, act, release":

1. Make the awaited condition observable: fake-tool invocation log line, gate-held invocation, a flag file the child writes, or mutex-protected shared state.
2. Poll it with the shared `waitUntil` until proven.
3. Perform the action (raise stop, rewrite file) — now guaranteed inside the window.
4. Release any gate / join and assert.

Alternatives: longer sleeps (still racy, slower), retry-on-failure (masks real regressions). Rejected.

### D2: One shared `testutils::waitUntil(predicate, timeout, pollInterval)` in `tests/test_utils.h`

Semantics follow the e2e copy (`encro_e2e_tests.cpp:1143`): loop `while (now < deadline) { if (pred()) return true; sleep(pollInterval); } return pred();` — the trailing re-check means a predicate that becomes true between deadline check and return is not reported as timeout. Default poll 25 ms / callers pass load-tolerant deadlines (10 s for subprocess proofs). The four existing copies (`waitUntil` in e2e, `waitUntilLogContains` in video_batch, the logging poll loop, the stop-signal poll loop) collapse into it; a variant reading a possibly-missing file without `REQUIRE` stays local to video_batch (reading a nonexistent log must not abort).

### D3: Fake-tool gating extends by call index, reusing the existing gate file

`ENCRO_FAKE_FFMPEG_GATE_FROM_CALL=N` + the existing `ENCRO_FAKE_FFMPEG_GATE_FILE`: invocation counter comes from the existing `ENCRO_FAKE_FFMPEG_CALL_COUNT_FILE` mechanism (the file tests already set when they need per-call behavior); invocations with count >= N log first, then block on the gate file; the 30 s fail-safe deadline and "unset from-call = gate everything" behavior carry over unchanged. This covers the mid-batch shape ("call 1 completes and caches, call 2 held") that a whole-run gate cannot express, and lets `pipeline_picture_tests` drop the 7000 ms `CALL_PLAN` delay entirely — the gate replaces the delay, so those tests get faster.

Alternative considered: a per-invocation gate file naming scheme — rejected, more knobs for no added expressive power.

### D4: Stop-window tests without the fake tool use a child-written flag file

`utils_tests.cpp` exec2 stop cases cannot poll a fake-tool log. The child command becomes a compound that touches a flag file before entering its long wait (e.g. `cmd /c "type nul > flag & ping -n 30 127.0.0.1 >nul"` / POSIX `sh -c 'touch flag; sleep 30'`); the test polls the flag, then requests stop. The PowerShell grandchild case keeps its pipe-holding semantics but swaps PowerShell for `cmd`/`ping` — PowerShell cold start under load is precisely what made its 2 s bound flaky.

### D5: Monitor tests poll the monitored state, not the clock

The encoding-monitor cases in `video_batch_execution_tests.cpp` already have the observable: `state->lastFrameCount` behind `state->mtx`. Poll it (predicate takes the lock) until the first parse result appears before rewriting the same-size file; poll for the final value instead of the trailing 400/500 ms sleeps. Ordering assertions (`seen` distinctness bounds) stay; they test throttling policy, which is real behavior, and their bounds are wide.

### D6: Job-state clock hook — free-function test hook, `system_clock` retained

`detail::nowMs()` in `src/core/job_state.cpp` becomes indirect through a settable function pointer (`jobstate::setClockForTest(fn)` / reset, mirroring `stop_signal`'s `setForceExitHandlerForTest` idiom) plus a scoped guard in `test_utils.h`. Tests then push synthetic timestamps and assert exact accumulated values (`== 30000`, not `>= 20`).

The clock stays `system_clock`: `startedAtMs`/`updatedAtMs` are persisted and compared across processes on resume, and `steady_clock`'s epoch is unspecified (often boot time), which would corrupt cross-run deltas. Switching to `steady_clock` was considered and rejected for that reason; the hook removes the test-side NTP sensitivity without touching persistence semantics.

### D7: Meta-check as a Catch2 test case with a marker-comment allowlist

A `TEST_CASE` in `tests/test_utils_tests.cpp` scans `tests/**/*.cpp` (source root injected as a compile definition the same way `FAKE_TOOL_EXE_PATH` already is) for `sleep_for` occurrences. Every occurrence must carry a `// sleep-ok: <reason>` marker on the same line; unmarked occurrences fail with file:line. Exempt files (they implement polling/the tool itself): `tests/e2e/fake_media_tool.cpp`, `tests/e2e/e2e_test_utils.cpp`. Scanning `.cpp` only keeps the shared header's own poll loop out of scope. A lint script or tidy hook was considered and rejected — a test-case guard runs everywhere the suite runs, in serial and parallel, with zero new tooling.

Measurement/delay sleeps that remain legitimately one-sided (e.g. `fake_tool_tests` elapsed lower bound, `probe_cache` timestamp ordering, the 30 ms signal-delay in the stop-signal wake test) keep their `sleep-ok` markers with reasons.

### D8: Shard logs get per-test durations; verdict parsing untouched

`plugins/test_parallel/xmake.lua` appends Catch2's duration-reporting flag to shard argv (exact flag form per Catch2 v3 docs, verified at implementation). Duration lines contain none of the verdict substrings (`FAILED`, `test cases:`, `assertions:`, `All tests passed`), so `shard_status` parsing is unaffected; the task includes a full `xmake test-parallel` run to confirm.

### D9: Elapsed upper bounds become hang guards

`elapsed < 1s/2s` assertions are relaxed to watchdog values (30 s) or dropped where join/ordering already proves completion (`stop_signal` wake test: `waitForStop(5s)` returning true is the assertion; the `< 1s` bound goes away). Rationale: bounds coupled to machine load test the machine, not the code; Catch2 has no per-test timeout, so generous bounds remain as hang protection.

## Risks / Trade-offs

- [Poll loops spin] → 10–25 ms interval, bounded by deadline; in practice each conversion removes a longer fixed sleep, so wall time improves.
- [Gate 30 s fail-safe fires under extreme load and silently changes test flow] → gates are released by the test immediately after the stop request; the fail-safe only guards miswired tests (same contract as the existing gate).
- [Meta-check false positives annoy contributors] → the `sleep-ok` marker is a one-line escape hatch with a required reason; exemptions cover the tool and e2e process helpers.
- [New shard-log output breaks `shard_status` heuristics] → duration lines don't contain verdict substrings; verified by a full parallel run before landing.
- [Clock hook left armed leaks into later test cases] → scoped guard resets it in destructor (same pattern as `ScopedStopSignalReset`).
- [Exact Catch2 duration flag unknown] → resolved at implementation from Catch2 v3 docs; worst case the flag is omitted and only D7/D1–D5 land — specs unaffected.

## Migration Plan

Pure test-infrastructure change plus one test-only hook; batches land independently and each is verified with a full `xmake test-parallel` run (ideally repeated 3× locally, since the failure mode is intermittent). Rollback is `git revert` of the batch; no persisted data or user-facing behavior is touched.
