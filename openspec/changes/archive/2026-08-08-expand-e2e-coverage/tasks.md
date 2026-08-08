## 1. Async process infrastructure

- [x] 1.1 Add `RunningProcess` (owns `bp::child` + jthread stdout/stderr drainers) and `runEncroAsync(args, env, cwd)` to `tests/e2e/e2e_test_utils.{h,cpp}`; keep existing synchronous `runProcess`/`runEncro` unchanged
- [x] 1.2 Add Windows process-group isolation: `new_process_group_` handler initializer (`handler_base` + `on_setup` setting `exec.creation_flags |= CREATE_NEW_PROCESS_GROUP`) used only on the async path; comment pinning boost 1.90 v1 pattern (see design D2)
- [x] 1.3 Add `sendCtrlC()` (Windows `GenerateConsoleCtrlEvent(CTRL_C_EVENT, id())`, POSIX `kill(id(), SIGINT)`), `terminate()`, `wait(timeout)`, `id()`; destructor terminates if not waited
- [x] 1.4 Add `consoleCtrlEventsAvailable()` probe (`GetConsoleWindow()` on Windows) returning false when events cannot be delivered
- [x] 1.5 Verify: build `e2e_tests`; existing 21 tests still green (infra is additive)

## 2. Interruption e2e tests

- [x] 2.1 Graceful Ctrl+C mid-encode: `DELAY_MS=15000`, poll fake-tool log until ffmpeg encode line appears, `sendCtrlC()` → exit code 130, state file exists with per-task status, `--resume` run completes and produces output (design D4)
- [x] 2.2 Hard-kill recovery: `DELAY_MS=15000`, wait for encode line, `terminate()` → state file intact, resume continues and finishes
- [x] 2.3 Cancel propagation: `-j 1` two inputs, Ctrl+C during task 1 → task 2 never attempted (`interrupted`, attemptCount 0); resume finishes both. (Deviation from design: "state file removed" is covered by unit tests `pipeline_picture_tests`; the all-tasks-unstarted window is a race in e2e)
- [x] 2.4 Console-unavailable environments: the three tests above `SKIP()` via `consoleCtrlEventsAvailable()` (assert the guard works by checking the probe in a helper test); probe uses `GetConsoleCP()` because `GetConsoleWindow()` is NULL under ConPTY

## 3. Fake toolchain opt-in enhancement

- [x] 3.1 Add `ENCRO_FAKE_FFPROBE_CHECK_INPUT=1`: ffprobe exits 2 with stderr when the probed path does not exist; default off, existing tests unaffected
- [x] 3.2 Add a test exercising probe failure end-to-end (missing input file → encro fails with a tool-probe error, non-zero exit); webp tolerates probe failure by design, so the e2e uses the default mp4 path (duration required)

## 4. mp4 real-ffmpeg smoke tests

- [x] 4.1 Fixture: `createRealSmokeVideoWithAudio` (testsrc2 + sine, `-c:v libx264 -c:a aac`) alongside existing fixture
- [x] 4.2 Smoke: `-f mp4` default path on a 2 s source → exit 0, probe output: default codec is `hevc`, audio stream present, file size > 0
- [x] 4.3 Segment resume smoke: 12 s source (forces 2 segments) → encode → delete final mp4 → resume → output recreated and probes as hevc. (Deviation from design: segment dirs are removed after success, so reuse is verified by fake-toolchain tests instead)

## 5. Picture-mode e2e tests

- [x] 5.1 `-t pic -c` compress happy path with fake toolchain: images in, compressed outputs + zip pack produced, exit 0
- [x] 5.2 `-t pic -c` failure path: partial failure (one output fails → exit 0, zip ships only the good file) and total failure (all fail → exit 1, `compress-phase` failed in state)
- [x] 5.3 `-t pic -s` folder-summary flat pack: summary image entry (`__summary__`) present and sorted first in zip, exit 0

## 6. Concurrency e2e test

- [x] 6.1 Multi-input `-j 2` with `ENCRO_FAKE_FFMPEG_DELAY_MS=3000` on both inputs: wall time < 5500 ms (serial would be ≥ 6000 ms), both outputs present and non-empty

## 7. Partial-failure e2e test

- [x] 7.1 Two explicit inputs, `ENCRO_FAKE_FFMPEG_FAIL_MATCH` matching one output: non-zero exit, successful input still encoded, state file shows one `succeeded` + one `failed` task

## 8. Overwrite prompt e2e test

- [x] 8.1 Spike (5 min): without `-y` and an existing output, `--restart` prompts "do you want to encode..." and EOF cancels; plain re-run without `--restart` auto-recovers from state without prompting; `-w` is ignored while job state exists. Also found: child stdin was inherited (prompt would block on the test runner's terminal) → `runChild`/`makeChild` now redirect `std_in < null`
- [x] 8.2 Test asserting the spiked behavior: prompt text + "canceled by user" + output untouched on EOF; `-y --restart` skips the prompt and re-encodes

## 9. Verification

- [x] 9.1 `xmake format -k check` clean; `xmake build tests && xmake run tests` green; `xmake build e2e_tests && xmake run e2e_tests` green (console-dependent tests SKIP gracefully if no console)
- [x] 9.2 `xmake run e2e_tests "[real-ffmpeg]"` with ffmpeg on PATH: all smokes pass including new mp4 ones
- [x] 9.3 Post-change review per CLAUDE.md (code-review skill); commit as conventional commits batched by functional area (test infra → tests → fake tool)
