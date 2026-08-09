# error-visibility-hardening — Tasks

Design has no open questions. Tasks are ordered by dependency: crash infrastructure (D6) first because the watchdog (D5) reuses its direct-write API; executor logging (D4) before pack consumption (D2); the largest surface (job-state, D1) after the smaller ones.

## 1. Crash-report durability (D6)

- [x] 1.1 Write failing tests: direct-write line format matches `kLogPattern` timestamp precision (milliseconds + `%z`); `tryWriteDirectToLogFile` succeeds after transient open failures (retry); crash report still reaches stderr when the log file is permanently unavailable (extend `tests/logging_crash_integration_test.cpp`)
- [x] 1.2 Add millisecond + timezone-offset precision to the direct-write timestamp in `src/infra/crash_runtime.cpp`
- [x] 1.3 Add bounded retry (3 attempts, ~10ms backoff) to `tryWriteDirectToLogFile` to cover the rotating-sink close→reopen window
- [x] 1.4 Expose `crash::writeDirectLogLine(std::string_view)` (public wrapper over tier-1 logic) and document the tier-2 async limitation (`_Exit` never drains the queue) in a code comment

## 2. Cancellation evidence (D5)

- [x] 2.1 Write failing tests: registering a stop request produces an info log record; force-exit watchdog writes a final direct line before termination (test via a short injected grace period in `tests/`)
- [x] 2.2 Log an info record when a stop request is registered in `src/infra/stop_signal.cpp` (Windows Ctrl-C handler path; POSIX keeps the handler signal-safe — log from the polling/watchdog path)
- [x] 2.3 Call `crash::writeDirectLogLine` with a force-exit explanation immediately before `ExitProcess(130)` in the watchdog

## 3. Task exception logging (D4)

- [x] 3.1 Write failing tests: a task that throws yields a failed result whose message contains the exception text, and an error-level log record with the message (extend `tests/task_executor_tests.cpp`)
- [x] 3.2 Add `LOG_ERROR` with the exception message in the catch block of `src/core/task_executor.cpp` (tag `core.task`)
- [x] 3.3 Update `src/video/video_batch_execution.cpp` so a failed result's message (not the generic `"encoding failed"`) becomes `state.lastError`

## 4. Pack failure truthfulness (D2)

- [x] 4.1 Write failing tests: a pack run where one task in a group throws is reported failed, the log records the exception message, and the summary does not claim full success (extend `tests/pack_service_mock_tests.cpp`)
- [x] 4.2 In `src/pack/pack_service.cpp` treat a group as failed when `packResults[index]` is failed OR any `runRes.results[i]` is failed; propagate the task error message into the group error

## 5. Job-state persistence failure reporting (D1)

- [x] 5.1 Write failing tests: `flushSnapshot`/`flushLocked` return an error when the state path is unwritable (invalid/read-only path); a failed save is logged with the operation name; `initialize` fails (not silently succeeds) when the initial flush fails (extend `tests/job_state_tests.cpp`)
- [x] 5.2 Change `detail::flushSnapshot` in `src/core/job_state.cpp` from `void` to `eh::Result<void>` (propagate open/write/rename `ec` with descriptive messages)
- [x] 5.3 Change `Store::flushLocked` to `eh::Result<void>`; keep `mark*`/`setStage`/`requestCancel`/`flush` as `void` and route every save through a private `persistLocked` helper that `LOG_ERROR`s failures (tag `core.job`); `initialize` returns `eh::Result` and propagates
- [x] 5.4 Update all call sites (video_encoding_state, video_batch_execution, video_process, picture_process, pipeline, pack_service): `initialize` failures propagate to the top-level error path; mutator call sites need no changes (failures are logged inside the store)

## 6. Scanner error reporting (D3)

- [x] 6.1 Write failing tests: unreadable/missing input root yields an error naming the root (not an empty result); mid-walk iteration failure yields a warning record; readable-but-empty yields the unchanged empty result (extend `tests/media_scanner_tests.cpp` if present, else the video/picture process tests)
- [x] 6.2 Change `src/core/media_scanner.cpp` entry/`scanDir` to return `eh::Result<std::vector<fs::path>>` (propagate root `is_directory` `ec`; collect iteration failures into a warnings list)
- [x] 6.3 Update `src/video/video_process.cpp` and `src/picture/picture_process.cpp`: unreadable root → error log + non-zero exit with a message naming the root; iteration warnings → `LOG_WARN`; empty result keeps current behavior

## 7. Periodic flush (D7)

- [x] 7.1 Write a test asserting that non-error lines written before a simulated hard stop (no `shutdown()`) are present after teardown-flush, and that repeated setup/shutdown pairing still works (extend `tests/logging_file_mgmt_test.cpp`)
- [x] 7.2 Register `spdlog::flush_every(std::chrono::seconds(1))` in `logging::setup()` in `src/logging/setup.cpp`

## 8. Forensic snapshot reflects the in-flight command (D8)

- [x] 8.1 Write failing test: a WebP retry-tier failure attaches a snapshot whose `subprocessCmdline` shows the failing tier's command, not the initial q=80 command (extend `tests/logging_infra_test.cpp` snapshot tests)
- [x] 8.2 In `src/video/video_encode_runner.cpp`, update `state.subprocessCmdline` before each WebP quality-tier attempt (not only the initial one)

## 9. Verification

- [x] 9.1 Run `xmake build tests && xmake run tests` — all suites green (new + existing, including the 25+ message-text assertions that must stay untouched)
- [x] 9.2 Run `xmake build e2e_tests && xmake run e2e_tests`
- [x] 9.3 Run `xmake format` and confirm no formatting drift; run `xmake coverage --summary` if time permits
