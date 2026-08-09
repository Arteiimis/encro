# structured-log-correlation Tasks

## 1. Attribute mechanism (D1, D2)

- [ ] 1.1 RED: extend `tests/logging_error_context_test.cpp` with a failing test for `ScopedLogAttributes` — frames serialize as ` [attrs: {...}]` suffix after the context suffix, innermost scope shadows outer keys, 16-frame cap with FIFO eviction; include a reset helper (mirroring `resetContextStack`) so frames do not leak across test cases
- [ ] 1.2 Implement `ScopedLogAttributes` in `src/logging/logging.h`: thread-local key-value frame stack (same cap/eviction as context frames) + `formatAttributeChain()` producing a JSON object string
- [ ] 1.3 Append the attribute suffix in `LOG_*` macros after the context chain (attrs suffix last, empty when no frames)
- [ ] 1.4 RED: extend `tests/logging_json_test.cpp` with failing assertions — `run_id`/`task_id`/`input` appear as top-level fields when attrs are active, absent otherwise; attrs suffix stripped from `message`; a record with BOTH a context chain and attrs parses to the correct `error_context` array AND correlation fields (context parsing must stop at the attrs marker, not swallow it into the last frame)
- [ ] 1.5 Implement `JsonFormatter` parsing: find the ` [attrs: ` marker, run `boost::json::parse` on the tail, merge fields into the record; make `extractErrorContext` treat the attrs marker as end-of-context so both suffixes coexist

## 2. run_id bootstrap and job-state alignment (D3)

- [ ] 2.1 RED: unit test — `logging::setup()` establishes a stable `runId()`; `logging::setRunId()` replaces it
- [ ] 2.2 Generate bootstrap run_id (UUID via existing `utils` helper) in `logging::setup()`; expose `runId()`/`setRunId()` in `setup.h`; reset in `shutdown()` for tests
- [ ] 2.3 RED: job-state test — `Store::initialize` with no existing state creates `jobId` equal to `logging::runId()`
- [ ] 2.4 Change `job_state_store.cpp` fresh-state path to adopt `logging::runId()` instead of a fresh `getUUID()`
- [ ] 2.5 RED: job-state resume test — restoring a persisted state calls `logging::setRunId(jobId)` so subsequent records align
- [ ] 2.6 Implement resume adoption in `Store::initialize` (both fresh and restored paths set the logging run_id)

## 3. task_id and input scopes (D4)

- [ ] 3.1 Add `input` field (optional path string) to `taskexec::TaskSpec` in `src/core/task_executor.h`
- [ ] 3.2 RED: task-executor test — records logged inside a task carry `task_id` = task id and `input` = the spec's `input` value; records outside carry neither
- [ ] 3.3 Implement `ScopedLogAttributes` scope in `task_executor` around task execution, pushing `task_id` + `input` from the `TaskSpec`
- [ ] 3.4 Align task ids at the construction sites: video `encode:<stablePathString(vidPath)>` (same expression as `makeEncodeTask`), pack `archive:<stablePathString(zipPath)>` (same expression as `makeArchiveTask`); keep picture `compress:<outputPath>`
- [ ] 3.5 Fill `input` at the three construction sites: video `vids[taskIndex]`, picture `task.inputPath`, pack `zipPath`
- [ ] 3.6 RED: tests — video task records carry `task_id` matching the job-state record id; pack task records carry `archive:<zipPath>`; picture task records carry `compress:<outputPath>`
- [ ] 3.7 rg check: no test asserts old `pack:0`/`encode:<raw>` `TaskSpec.id` formats or `Task pack:0 threw` message text before finalizing

## 4. Millisecond JSON timestamps (D5)

- [ ] 4.1 RED: `logging_json_test` asserts `timestamp` matches `YYYY-MM-DDTHH:MM:SS.sssZ` with three fractional digits
- [ ] 4.2 Update `JsonFormatter::formatTimestamp` to append explicit `.sssZ` fraction from millisecond precision

## 5. End-of-run summary (D6)

- [ ] 5.1 RED: logging test — pass-through counting sink increments per-level counters and forwards records unchanged
- [ ] 5.2 Implement counting sink in `src/logging/setup.cpp`, inserted first in the sink chain
- [ ] 5.3 RED: logging test — `logRunSummary()` emits an NDJSON record whose last field is `summary` (status, jobId, tasks_total, tasks_failed, elapsed_ms, log, level_counts) and a human-readable `RUN SUMMARY:` line to the `.log` format
- [ ] 5.4 Implement `logRunSummary()` in `setup.h`/`setup.cpp` (both formats through the normal logger)
- [ ] 5.5 Wire summary emission into `app_entry` at the two choke points: `failWithHint` (parse/config/toolchain/pipeline failures; summary logged before its existing `logging::shutdown()` drain) and the success branch of `runAppPipeline` (success and Ctrl-C; explicit `logging::shutdown()` after the summary, since these paths currently return without draining)
- [ ] 5.6 RED/e2e: successful run via fake tool — summary is the last `.ndjson` record with `status` `success` and correct counts; pipeline-failure run — summary reports `status` `failed`
- [ ] 5.7 Test: interrupted run (stop signal) produces `status: interrupted` summary; verify summary is the last record (no records after)

## 6. Verification

- [ ] 6.1 Full unit test suite green (`xmake build tests && xmake run tests`)
- [ ] 6.2 E2E suite green with fake tool (`xmake build e2e_tests && xmake run e2e_tests`); manual sanity run with `--log-json` on a small batch: records carry run_id/task_id/input, ms timestamps, summary last
- [ ] 6.3 `xmake format` applied; `openspec validate` passes for the change
