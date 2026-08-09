# structured-log-correlation

## Why

The primary consumer of encro's verbose logs is an AI assistant performing post-incident diagnosis. Today the NDJSON records are self-contained but not self-correlating: parallel task logs interleave with no run/task identifier, JSON timestamps lack the millisecond precision the human-readable format already has, there is no end-of-run summary line, and the JSON schema exists only in `json_formatter.h` code — every AI reader must reverse-engineer it before it can reason about the log.

## What Changes

- NDJSON records gain structured correlation fields: `run_id` (aligned with the existing job-state `jobId`) and `task_id` (aligned with `TaskRecord.id`), plus an `input` field carrying the per-file path that `ScopedErrorContext` already tracks as prose.
- Correlation attributes are captured at the call site (thread-local, RAII scoped) and serialized into the message before the async write, so the JSON formatter can parse them back out — the same mechanism `error_context` already uses.
- JSON timestamps gain millisecond precision, matching the human-readable sink pattern (`%e`).
- A single machine-readable end-of-run summary record is written at exit (status, task counts, level counts, elapsed, jobId, log path) in both `.log` and `.ndjson` formats.
- The NDJSON schema (fields, types, semantics, level→syslog mapping) is documented in the `logging-behavior` spec so readers do not need to read source code to interpret records.
- No CLI flags change; `--log-json` remains opt-in. No existing field is renamed or removed (non-breaking).

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `logging-behavior`: NDJSON record schema gains correlation fields (`run_id`, `task_id`, `input`), millisecond-precision timestamps, an end-of-run summary record, and a documented field schema with level mapping.

## Impact

- **Code**: `src/logging/logging.h` (attribute frames + call-site serialization), `src/logging/json_formatter.h` (field parsing, ms timestamps), `src/logging/setup.h`/`setup.cpp` (summary API, run_id propagation), `src/core/job_state.cpp` (run_id source), `src/core/task_executor.cpp` (task_id scope), `src/pack/pack_service.cpp` (pack task id + input), `src/video/video_batch_execution.cpp` / `src/picture/picture_compress.cpp` (task id + input construction), `src/video/video_info.cpp` (probe tasks — no scope), `src/app/pipeline.cpp` / `src/app/app_entry.cpp` (summary emission).
- **API**: internal only — new `logging` helpers; no CLI or user-visible message changes.
- **Behavior**: NDJSON record content changes (additional fields, ms timestamps); human-readable `.log` gains one summary line at the end. Existing consumers that parse NDJSON by exact field set would need to ignore unknown fields (none known today).
- **Tests**: formatter unit tests for new fields and summary record (extend `logging_json_test.cpp` / `logging_error_context_test.cpp`); correlation scoping tests via the existing task-executor test harness.
