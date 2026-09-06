# Proposal: stabilize-parallel-tests

## Why

`xmake test-parallel` (8 unit + 4 e2e shards on one machine) intermittently fails individual test cases that pass on re-run. The parallel architecture — process-per-shard, per-shard TMP/TEMP roots, log-based verdicts — is sound; every observed flake mechanism lives inside test bodies that synchronize on wall-clock time (fixed `sleep_for` before a stop request, fixed waits for async effects, tight elapsed-time upper bounds). Under shard load, Windows process spawn and thread scheduling latency is unbounded and these assumptions break. We want zero timing flakes without giving up parallelization or masking failures with retries.

## What Changes

- Add a shared `waitUntil(predicate, deadline)` poll helper in `tests/test_utils.h` (currently duplicated 4+ times across suites) and standardize "poll observable state, never sleep-and-hope" as the synchronization convention for tests.
- Extend the fake media tool with `ENCRO_FAKE_FFMPEG_GATE_FROM_CALL=N`: invocations from the Nth onward block on the existing gate file after logging, so a test can prove invocation N is in flight while invocations 1..N-1 ran to completion (today's gate blocks every invocation).
- Convert the timing-sensitive tests to state-based synchronization:
  - `tests/app/pipeline_picture_tests.cpp` (6 cases): fixed 1200 ms sleep before `requestStop()` → poll invocation log for the second call, then stop.
  - `tests/video/video_encode_runner_tests.cpp` stop-window case: fixed 250 ms sleep → gate + poll + stop + release.
  - `tests/utils_tests.cpp` exec2 stop cases: fixed 150 ms sleep → child writes a "started" flag file first; poll the flag; drop the PowerShell child whose cold start exceeds the 2 s bound.
  - `tests/video/video_batch_execution_tests.cpp` monitor cases: fixed 400/500 ms waits → poll the shared monitor state (e.g. `lastFrameCount`) for the awaited value.
  - `tests/logging_file_mgmt_tests.cpp` periodic-flush case: remove the racy negative assertion ("line NOT on disk yet" races the global 1 s flusher); keep the positive poll.
- Reframe elapsed-time upper bounds (`elapsed < 1s/2s`) as pure hang guards: completion ordering stays the assertion, bounds relax to a generous watchdog value.
- Add a test-only clock hook for `jobstate` (same idiom as `stop_signal`'s `setForceExitHandlerForTest`) so wall-clock accumulation tests (`job_state_tests.cpp`, `video_batch_execution_tests.cpp` elapsed cases) can push synthetic time instead of sleeping against `system_clock` (which NTP can roll back mid-test).
- Guardrails: shard logs record per-test durations (`--durations`), and a meta-check rejects new bare `sleep_for` uses in `tests/` outside an explicit allowlist (measurement sleeps with a marker comment stay allowed).

Non-goals: no changes to shard counts, TMP isolation, or log-based verdicts in `plugins/test_parallel`; no in-shard test-case parallelism; no automatic retry of failed cases.

## Capabilities

### New Capabilities

- `deterministic-test-sync`: how tests synchronize with async and subprocess activity — observable-state polling via a shared helper, hang-guard time bounds, a controlled clock for elapsed-accumulation tests, per-test durations in shard logs, and the no-bare-sleep rule for test synchronization.

### Modified Capabilities

- `portable-fake-tool`: new requirement — invocation gating from a configurable call index (`ENCRO_FAKE_FFMPEG_GATE_FROM_CALL`), extending the existing environment-variable control mechanism so mid-batch cancellation tests can hold a later invocation while earlier ones complete.

## Impact

- Test infrastructure: `tests/test_utils.h`, `tests/e2e/fake_media_tool.cpp`, `tests/test_utils_tests.cpp` (helper coverage).
- Test bodies: `tests/app/pipeline_picture_tests.cpp`, `tests/video/video_encode_runner_tests.cpp`, `tests/utils_tests.cpp`, `tests/video/video_batch_execution_tests.cpp`, `tests/job_state_tests.cpp`, `tests/logging_file_mgmt_tests.cpp`, plus dedup of local `waitUntil` copies in `tests/e2e/`.
- Production code (test seam only, no behavior change): the single `jobstate::detail::nowMs()` definition in `src/core/job_state.cpp` gains a test-settable clock behind the same pattern already used by `src/infra/stop_signal` test hooks; all callers funnel through it unchanged.
- Build tooling: `plugins/test_parallel/xmake.lua` forwards `--durations` to shard processes; new meta-check for bare sleeps runs as part of the unit suite.
