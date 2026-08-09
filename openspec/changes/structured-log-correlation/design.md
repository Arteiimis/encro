# structured-log-correlation Design

## Context

See proposal.md — Why. The constraints that shape this design:

- Logging is **asynchronous** (spdlog thread pool, 8192 queue, block policy): the JSON formatter runs on the pool thread, so it **cannot read caller-thread state** at write time. Structured data must be serialized into the message string at the call site, then parsed back out by the formatter. The existing `error_context` mechanism (`ScopedErrorContext` → ` [context: ...]` suffix → parsed array) already follows this pattern.
- `jobstate::Store` already owns a `jobId` (UUID via `utils::getUUID()`); `TaskRecord` has `id` and `sourcePaths`. Task execution funnels through `task_executor` (video encode, pack, picture tasks), which is the natural single scope point.
- `logging/setup.h` already exposes `EnvironmentSnapshot` and a forensic-snapshot pipeline; `JsonFormatter` already parses suffixes out of the message.
- Human `.log` pattern already has milliseconds (`%e`); the JSON formatter is the one missing them.

## Goals / Non-Goals

**Goals:**
- Add `run_id`, `task_id`, `input` correlation fields to NDJSON records with zero changes to the 120 existing log call sites.
- Millisecond JSON timestamps; end-of-run summary record in both formats; schema documented in the delta spec.
- Preserve the existing two-sink split (human `.log` always, NDJSON opt-in via `--log-json`).

**Non-Goals:**
- No renaming of existing fields, no ECS/Logstash field adoption, no `severity_number`, no default-on NDJSON, no progress-noise filtering, no changes to the console sink or CLI.

## Decisions

### D1: Call-site serialized attributes (JSON-encoded suffix), not formatter-side TLS reads

Attribute frames are pushed onto a thread-local stack by RAII scopes; the `LOG_*` macros serialize the active frames as a JSON object appended to the message (` [attrs: {"run_id":"...","task_id":"...","input":"..."}]`); `JsonFormatter` extracts and merges it into the record.

- **Why JSON inside the suffix**: Windows paths routinely contain spaces and quotes; key=value or delimiter-split parsing is ambiguous. `boost::json` is already a dependency; the formatter runs `boost::json::parse` on the text after the ` [attrs: ` marker and checks the remainder is `]` (JSON string values may themselves contain `]`, so position-based slicing to the last `]` is not used). Values are escaped by the serializer for free.
- **Alternatives rejected**: formatter-side TLS read (broken under async — wrong thread); spdlog named loggers per task (logger registry explosion); passing attributes through spdlog's `log_msg` (spdlog has no generic attribute channel).

### D2: New `ScopedLogAttributes` sibling to `ScopedErrorContext`, sharing the serialization pipeline

A small RAII type (`ScopedLogAttributes({{"key","value"},...})`) maintaining a thread-local key-value frame stack (same 16-frame cap and FIFO eviction as context frames), serialized by a new `formatAttributeChain()` appended after the context suffix. `error_context` keeps its array shape; attributes merge as top-level record fields.

- **Why not extend `ContextFrame`**: contexts are a semantic list (`stage(detail)` frames); attributes are key-value pairs. Two shapes, two suffixes — keeps existing formatter parsing and tests untouched.
- **Why a stack (not one global map)**: nested scopes (run → task → file) need push/pop; stack top wins per key when serializing (innermost scope shadows).

### D3: `run_id` ownership in logging, adopted by job state

`logging::setup()` generates the bootstrap `run_id` (UUID); `Store::initialize` adopts it as the fresh `jobId` (one-line change in `job_state_store.cpp` where `getUUID()` is called today). On resume, `Store` restores the persisted `jobId` and calls `logging::setRunId(jobId)` so post-init records align with the state file. `runId()` SHALL return a lazily generated default when queried before `setup()`, so Store unit tests that never call `logging::setup()` still get a non-empty id.

- **Alternatives rejected**: passing run_id through `Store::initialize` signatures (ripples into tests and callers); a separate `run_id` that never equals `jobId` (breaks cross-verification between `.ndjson` and `job_state.json` — the whole point).
- Records emitted before job-state init (startup, tool check — a handful) carry the bootstrap id; the delta spec allows this explicitly.

### D4: `task_id`/`input` scope at `task_executor`, with executor-agnostic task ids

Verified: the three business task paths (video encode, pack, picture compress) funnel through `taskexec::runTasks` (`video_batch_execution.cpp`, `pack_service.cpp`, `picture_compress.cpp`) — one scope point covers them. Two auxiliary probe/prewarm paths (`video_info.cpp`) also use `runTasks` but construct no `input`; the executor pushes the scope only when `TaskSpec.input` is set, so probe records carry neither `task_id` nor `input`. `TaskSpec` gains a new `input` field (optional path string); the three business construction sites each fill it (video: `vids[taskIndex]`, picture: `task.inputPath`, pack: `zipPath`).

`TaskSpec.id` is aligned with job-state ids at the construction sites so logs can join `job_state.json` directly:
- video: `encode:<stablePathString(vidPath)>` — same expression as `makeEncodeTask`
- pack: `archive:<stablePathString(zipPath)>` — same expression as `makeArchiveTask` (zipPath is already in scope)
- picture: kept as `compress:<outputPath>` — job-state tracks a single phase-level `compress-phase` task, so per-file ids cannot (and need not) align; the delta spec states this exception

Verified non-coupling: `mergeTasks` matches resumed tasks by `TaskRecord.id`, and segment directories hash `state.actionId` (which is the `TaskRecord.id` from `actionIds`), not `TaskSpec.id` — changing `TaskSpec.id` affects neither resume nor segment dirs. No test asserts `TaskSpec.id` formats (`pack:0`/`encode:` only appear in the construction sites themselves).

### D5: Millisecond timestamps via explicit fraction

`formatTimestamp` switches from `{:%Y-%m-%dT%H:%M:%SZ}` to `{:%Y-%m-%dT%H:%M:%S}` plus a manually formatted `.sssZ` from `duration_cast<milliseconds>` of the time point. Matches the human pattern's precision.

### D6: End-of-run summary via a `logRunSummary()` API + pass-through counting sink

- `logging::logRunSummary(SummaryData)` builds one record: NDJSON gets `{"summary": {...}}` (status, jobId, tasks_total, tasks_failed, elapsed_ms, log path, level_counts); the human sink gets a `RUN SUMMARY: status=... key=value` line. Both go through the normal logger so ordering/flush semantics are unchanged.
- `level_counts` comes from a tiny pass-through counting sink inserted first in the sink chain (atomics per level, incremented in `sink_it_`, forward to next sink). ~15 lines; avoids touching the 120 call sites or the formatters.
- `logRunSummary` attaches the current `run_id` explicitly — the summary is emitted outside any task scope, so the attribute stack is empty there.
- Emitted at two choke points in `app_entry`: (1) `failWithHint` covers parse, config, toolchain, and pipeline failures — the summary is logged before its existing `logging::shutdown()` drain; (2) the success branch of `runAppPipeline` covers success and Ctrl-C — today those paths return without any drain, so the summary MUST be followed by an explicit flush (`logging::shutdown()`), otherwise it is lost in the async queue. The hard-kill watchdog path cannot run it (process exits immediately) — accepted, the crash report covers that case.

### D7: Schema documentation lives in the delta spec

The field table and level→syslog mapping are spec content (they are observable output behavior), not prose — see `specs/logging-behavior/spec.md`.

## Risks / Trade-offs

- [Attribute suffix increases message size] → Only for records emitted inside scopes; frames are 2–3 short strings; negligible vs. 10MB rotating files.
- [Resume runs have two run_id values (bootstrap vs. restored jobId)] → Pre-init records are few and carry no task_id; task-level correlation (the actual diagnostic need) is unaffected.
- [Summary `level_counts` counts records, not events] → Documented semantics in the spec; counts per level are still a good noise/severity signal for the reader.
- [Formatter now parses two suffixes] → Same pattern as today's context parsing; existing tests pin the context behavior, new tests pin attrs; parsing stays single-pass over the tail of the string.
- [Manual `.sssZ` formatting] → std::format `%S` gives seconds only; the manual fraction is 3 lines and unit-testable.

## Migration Plan

- Additive only: new fields appear in NDJSON records; existing parsers ignoring unknown fields are unaffected; no CLI change; no state-file format change (jobId already exists there).
- Rollback: revert the change; `.ndjson` returns to today's schema, `.log` loses the summary line.

## Open Questions

None. Both implementation-time verifications from the original draft are resolved: (1) all task paths funnel through `task_executor` — verified; (2) `TaskSpec` gains an explicit `input` field instead of relying on `TaskRecord.sourcePaths` — decided in D4.
