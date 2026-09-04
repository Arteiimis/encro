# Tasks: stabilize-parallel-tests

## 1. Shared test-sync foundations

- [ ] 1.1 Add `testutils::waitUntil(predicate, timeout, pollInterval)` to `tests/test_utils.h` (deadline as hang guard, trailing predicate re-check) with coverage in `tests/test_utils_tests.cpp` (true when predicate holds before deadline without waiting it out; false after deadline; no silent pass). Verify: `xmake test-report --tag="[test-utils]"` passes.
- [ ] 1.2 TDD the job-state clock hook: write failing tests in `tests/job_state_tests.cpp` that set a synthetic clock, mark running/interrupted around a pushed timestamp delta, and assert exact accumulated values (equality, not bounds); then implement the free-function hook (`setClockForTest`/reset, mirroring `stop_signal`'s test-hook idiom) plus a scoped guard in `tests/test_utils.h`, keeping `system_clock` as the default clock. Verify: `xmake test-report --tag="[job-state]"` passes.
- [ ] 1.3 Inject the source root into the tests target as a compile definition (`ENCRO_TEST_SOURCE_DIR`, same mechanism as `FAKE_TOOL_EXE_PATH`) so in-suite meta-checks can locate `tests/` sources from the built binary. Verify: build succeeds and a temporary probe test lists `tests/test_utils.h` under the injected path.

## 2. Fake tool: gate from call index

- [ ] 2.1 TDD `ENCRO_FAKE_FFMPEG_GATE_FROM_CALL`: failing tests in `tests/fake_tool_tests.cpp` covering (a) gate-from-call=2 — first invocation completes normally, second blocks after logging its invocation, creating the gate file releases it; (b) from-call unset — every invocation gates (current behavior unchanged). Verify: new cases fail for the right reason before implementation.
- [ ] 2.2 Implement `ENCRO_FAKE_FFMPEG_GATE_FROM_CALL` in `tests/e2e/fake_media_tool.cpp` (invocation index from the existing call-count file; index >= N gates after logging; 30 s fail-safe and whole-run gate behavior preserved). Verify: `xmake test-report --tag="[fake-tool]"` passes.

## 3. Convert stop-window tests (poll-proof, stop, release)

- [ ] 3.1 `tests/app/pipeline_picture_tests.cpp` — all 6 cancel-mid-batch cases: replace the 1200 ms sleep with gate-from-call=2 + poll the invocation log until the second invocation is recorded, then `requestStop()`, then create the gate file; drop the 7000 ms `CALL_PLAN` delays. Verify: `xmake test-report --tag="[pipeline]"` passes and the cancel cases no longer depend on any fixed delay.
- [ ] 3.2 `tests/video/video_encode_runner_tests.cpp` stop-window case: hold the first attempt on the existing whole-run gate, poll the invocation log for proof it started, stop, release; remove the 250 ms sleep and the "assertions only when the file exists" hedge. Verify: `xmake test-report --tag="[video-encode-runner]"` passes.
- [ ] 3.3 `tests/utils_tests.cpp` exec2 stop cases: children touch a flag file before their long wait; poll the flag before requesting stop; replace the PowerShell child with `cmd`/`ping` (same pipe-holding semantics, no cold-start variance); relax `elapsed < 2s/5s` to 30 s hang guards. Verify: `xmake test-report --tag="[utils]"` passes.
- [ ] 3.4 `tests/infra/stop_signal_tests.cpp` wake test: drop the `< 1s` elapsed bound (`waitForStop(5s)` returning true is the assertion); mark the 30 ms signal-delay sleep `sleep-ok`. Verify: `xmake test-report --tag="[stop-signal]"` passes.

## 4. Convert async-effect waits and measurement sleeps

- [ ] 4.1 `tests/video/video_batch_execution_tests.cpp` monitor stat-skip case: poll `state->lastFrameCount` (under its mutex) until the first parse result (10) before rewriting the same-size file, and poll the final value instead of the trailing sleeps. Verify: `xmake test-report --tag="[video-batch-execution]"` passes.
- [ ] 4.2 Same file, remaining monitor cases: throttle case keeps the 40 ms append cadence as the input signal (marked `sleep-ok`) and replaces the trailing 400 ms wait with a poll for the final frame value, keeping the wide distinctness bounds; monitor-exits-after-stop case drops the `< 1s` elapsed bound (join ordering is the assertion, bound relaxed to a hang guard). Verify: monitor cases pass under a loaded parallel run.
- [ ] 4.3 `tests/job_state_tests.cpp` and `tests/video/video_batch_execution_tests.cpp` elapsed-accumulation cases: drive the synthetic clock from task 1.2 with exact equality assertions; delete the 20–30 ms measurement sleeps. Verify: `[job-state]` and `[video-batch-execution]` tags pass with no measurement sleeps left in either file.
- [ ] 4.4 `tests/logging_file_mgmt_tests.cpp` periodic-flush case: delete the negative "line not on disk yet" check (races the global 1 s flusher), keep the positive appearance poll via `waitUntil`. Verify: `xmake test-report --tag="[file_mgmt]"` passes.
- [ ] 4.5 Deduplicate poll loops: replace the local `waitUntil` in `tests/e2e/encro_e2e_tests.cpp` and any remaining log-poll copies with the shared helper where semantics match. Verify: e2e build + `xmake run e2e_tests` passes.

## 5. Guardrails

- [ ] 5.1 Meta-check as a `TEST_CASE` in `tests/test_utils_tests.cpp`: scan `tests/**/*.cpp` (excluding `tests/e2e/fake_media_tool.cpp`, `tests/e2e/e2e_test_utils.cpp`) via `ENCRO_TEST_SOURCE_DIR` for `sleep_for` occurrences lacking a `// sleep-ok:` marker on the same line; fail with file:line. Add `sleep-ok` markers (with reasons) to the remaining legitimate sleeps (`fake_tool_tests` elapsed lower bound, `probe_cache` timestamp ordering, e2e mtime gap, throttle cadence). Verify: check passes green; a deliberately unmarked sleep makes it fail naming file:line; remove the probe.
- [ ] 5.2 `plugins/test_parallel/xmake.lua`: append Catch2's per-test duration flag to shard argv (verify exact v3 flag form) without disturbing `shard_status` verdict parsing. Verify: full `xmake test-parallel` passes and shard logs contain per-test durations.

## 6. Final verification and docs

- [ ] 6.1 Run `xmake test-parallel` three times consecutively — all runs green, no timing failures; confirm total wall time is not worse than the pre-change baseline (~18 s). Record the outcome in the change notes.
- [ ] 6.2 Document the convention in `AGENTS.md` (Testing section): tests synchronize by polling observable state, never fixed sleeps; any `sleep_for` needs a `// sleep-ok: <reason>` marker (enforced by the meta-check). Verify: docs match the implemented rule.
