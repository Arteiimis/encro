# error-visibility-hardening

## Why

A full audit of the logging system found that post-incident diagnosability is bounded not by log coverage but by error handling: several failure paths silently discard errors (state-file saves are `void`, scanner ignores filesystem errors, pack task exceptions are reported as success), and cancellation and crash paths leave weak or no evidence. When something goes wrong in the field, the log must contain a truthful, attributable record — today it sometimes contains nothing, or worse, a false success.

## What Changes

- `job_state` persistence failures (disk full, read-only, locked state file) are propagated and logged instead of silently swallowed (`flushSnapshot`/`flushLocked` become error-returning; all `mark*` callers check and log).
- Pack task exceptions are no longer reported as success: `pack_service` checks per-task results and logs the failure.
- `media_scanner` distinguishes "directory unreadable" from "no matching files" and reports the former as a warning/error instead of exiting 0 with "No encodable videos found".
- Encoding task exceptions (thrown inside `task_executor` workers) are logged with the exception message.
- Ctrl-C cancellation leaves a log trail (`stop_signal` logs the signal); the 3-second force-exit watchdog writes a direct final log line before `ExitProcess`.
- Progress parsing degradation is logged: `parseProgressFile` failure and `parseSegmentEndUs` fallback emit warnings instead of silently degrading.
- Crash report durability is hardened: the async tier-2 fallback blind spot (messages posted but never drained under `_Exit`) is closed by making the direct-file tier primary and fallible-safe; direct-write format gains millisecond precision and `%z` to match the spdlog pattern; a periodic flush (`flush_every`) bounds loss of recent non-error lines on hard kill.
- Forensic snapshot reflects the actual in-flight command: WebP retry tiers update `subprocessCmdline` instead of leaving the stale initial command.
- **Backlog (NOT in scope)**: structured error codes in `eh::Result` — deferred to a future change (78 signatures, 101 call sites, 25+ test assertions pinning message text).

## Capabilities

### New Capabilities

- `error-visibility`: contract that every operational failure, cancellation, and crash leaves a truthful, attributable, durable record in the log file — covering state persistence, task exceptions, scan failures, cancellation evidence, progress-parse degradation, and crash-report durability.

### Modified Capabilities

(none — existing spec-level behavior of `logging-behavior`, `subprocess-exec`, `job-state-resume-matching`, and `video-frame-resume` is unchanged; this change only adds records where today there are none)

## Impact

- **Code**: `src/core/job_state.cpp`, `src/core/job_state_store.cpp`, `src/core/media_scanner.cpp`, `src/core/task_executor.cpp`, `src/infra/stop_signal.cpp`, `src/infra/crash_runtime.cpp`, `src/logging/setup.cpp`, `src/pack/pack_service.cpp`, `src/video/video_progress_parser.cpp`, `src/video/video_encoding_state.cpp`, `src/video/video_encode_runner.cpp`, `src/video/video_process.cpp`.
- **API**: internal only — `Store::flush*`/`flushSnapshot` signatures gain error reporting; no CLI or user-visible message changes (except new warning lines on console for scan failure and progress degradation).
- **Behavior**: exit code for "input directory unreadable" may change from 0 to non-zero (currently a false success).
- **Tests**: new unit tests for each surfaced error path; existing message-text assertions are unaffected (no error-text rewording).
