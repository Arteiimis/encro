# Roadmap: Encro -- 日志系统优化

## Overview

Four phases transform encro's flat, single-file logging into a production-grade diagnostics system. Phase 1 establishes the macro-and-tag foundation so every log line carries source location and module identity. Phase 2 adds per-run file isolation, automatic retention, and stage-level timing. Phase 3 introduces error context chains and environment snapshots for zero-reproduction debugging. Phase 4 layers on structured JSON output for toolchain integration -- all built on spdlog without new dependencies.

## Phases

- [x] **Phase 1: Logging Foundation** - Macros, module tag convention, logger registry, and centralized config (completed 2026-05-22)
- [x] **Phase 2: File Management + Runtime Observability** - Per-run timestamped files, retention cleanup, and scoped stage timing
- [x] **Phase 3: Forensics** - Error context chain traceback and environment snapshots on failure
- [ ] **Phase 4: JSON Tooling** - NDJSON structured output via --log-json with custom formatter

## Phase Details

### Phase 1: Logging Foundation

**Goal**: Every log line automatically carries source location and module identity -- developers write LOG_INFO("msg") and get `file.cpp:128 [video.encode]` for free, with all config centralized in one place.
**Depends on**: Nothing (first phase)
**Requirements**: INF-01, INF-02, INF-03, INF-04, INF-05, OBS-01, OBS-02, OBS-04
**Success Criteria** (what must be TRUE):

  1. Developer can use LOG_INFO/LOG_DEBUG/LOG_WARN/LOG_ERROR/LOG_CRITICAL macros in any .cpp file, and each log line automatically includes source file path and line number (e.g., `codec_transcoding.cpp:247`) without manually passing `__FILE__`/`__LINE__`.
  2. Each .cpp file registers a module logger with a single DEFINE_LOGGER("video.encode") call, and every log line from that file carries the module tag (e.g., `[video.encode]`).
  3. All logger creation, sink wiring, and spdlog registration is confined to `src/logging/setup.cpp` -- business code files contain only `DEFINE_LOGGER` and `LOG_*` macro invocations, never direct spdlog API calls.
  4. Module tag constants are declared in a single header (`src/logging/log_tags.h`) with a documented dot-notation hierarchy (`video.encode`, `pack.zip`), and all DEFINE_LOGGER calls reference these constants -- no ad-hoc tag strings exist anywhere.

**Plans**: 4 plans in 2 waves

Plans:

- [x] 01-01-PLAN.md — Logging infrastructure: log_tags.h, logging.h, setup.h, setup.cpp, TDD tests (Wave 1)
- [x] 01-02-PLAN.md — Build system (SPDLOG_ACTIVE_LEVEL) + prelude.cpp refactoring (Wave 2)
- [x] 01-03-PLAN.md — Migration of 11 spdlog-active files to LOG_* macros (Wave 2)
- [x] 01-04-PLAN.md — DEFINE_LOGGER for 10 files without current spdlog usage (Wave 2)

### Phase 2: File Management + Runtime Observability

**Goal**: Each CLI invocation produces a self-contained, time-aware log file with automatic lifecycle management -- users get per-run isolation, automatic cleanup, and stage-level timing in every log.
**Depends on**: Phase 1
**Requirements**: FILE-01, FILE-02, FILE-03, FILE-04, FILE-05, OBS-03
**Success Criteria** (what must be TRUE):

  1. Running `encro` generates a uniquely timestamped log file (e.g., `encro_20260523_143052.log`) in `%LOCALAPPDATA%/encro/logs/` -- each invocation creates its own file, never appending to or overwriting a previous run's output.
  2. On startup, encro automatically scans the log directory and deletes files beyond the 10 most recent runs, keeping disk usage bounded without user intervention.
  3. The encoding pipeline logs entry and exit of each major stage (scan, probe, encode, pack) with elapsed time -- developers add timing by placing a single RAII guard at stage entry, and the elapsed duration appears in the log on scope exit.
  4. If the log directory cannot be created or written to (e.g., permissions, disk full), encro falls back to a temporary directory and continues execution -- the user's encoding workflow is never blocked by logging infrastructure failures.
  5. When the crash handler fires, the final critical log message and any buffered spdlog content are appended directly to the current run's log file via a bypass path, ensuring crash diagnostics survive even if the async queue has been drained.

**Plans**: 4 plans in 2 waves

Plans:

- [x] 02-01-PLAN.md — File management: timestamped naming, retention cleanup, rotating sink, fallback chain, currentLogFilePath() (Wave 1)
- [x] 02-02-PLAN.md — ScopedTimer: RAII stage timing class with TDD tests (Wave 1)
- [x] 02-03-PLAN.md — Crash handler: direct file append bypass path, 3-tier fallback chain (Wave 2)
- [x] 02-04-PLAN.md — Pipeline instrumentation: ScopedTimer at video/picture/pack stage entry points (Wave 2)

### Phase 3: Forensics

**Goal**: When an error occurs, the log contains a complete diagnostic chain -- what was being processed, at which stage, after how many attempts, and the system state at failure time -- enabling root-cause identification without reproduction.
**Depends on**: Phase 2
**Requirements**: FOR-01, FOR-02, FOR-03
**Success Criteria** (what must be TRUE):

  1. When an error is logged, the output includes a full operation context chain: which input file, which pipeline stage, how many retries, and the specific error -- e.g., "input.mkv -> encode stage -> retry 2/3 -> FFmpeg exit code 1".
  2. On error, the log captures a snapshot of the encoding runtime: which concurrent slots were active, how many files remained in the queue, and the FFmpeg subprocess state -- answering "what else was happening?" at failure time.
  3. Error context is accumulated automatically via RAII scoped guards -- developers add context by placing `ScopedErrorContext ctx("stage", detail)` at function boundaries, and the full accumulated chain is serialized inline whenever any `LOG_ERROR` fires within that scope, without requiring manual context threading or spdlog MDC.

**Plans**: 3 plans in 3 waves

Plans:
**Wave 1**

- [x] 03-01-PLAN.md — ScopedErrorContext RAII class, TLS context stack, and formatContextChain() (TDD, Wave 1)

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 03-02-PLAN.md — LOG_ERROR/LOG_CRITICAL context chain injection and environment snapshot (TDD, Wave 2)

**Wave 3** *(blocked on Wave 2 completion)*

- [x] 03-03-PLAN.md — Pipeline placement of ScopedErrorContext at all stage boundaries and retry loops (Wave 3)

### Phase 4: JSON Tooling

**Goal**: Logs can be emitted as structured NDJSON for programmatic consumption (log analyzers, CI pipelines) while console output remains human-readable.
**Depends on**: Phase 3
**Requirements**: TOOL-01, TOOL-02, TOOL-03
**Success Criteria** (what must be TRUE):

  1. Running `encro --log-json` writes a companion NDJSON file (one JSON object per line) where each line contains `timestamp`, `level`, `module`, `source_location`, `message`, and optional `elapsed_ms`/`error_context` fields as structured keys.
  2. When `--log-json` is active, the terminal/stderr output continues to display human-readable text lines -- JSON is written only to the file sink, preserving developer experience at the console.
  3. JSON output correctly handles edge cases in log message content: Windows file paths with backslashes, Unicode text from FFmpeg (CJK error messages), embedded double quotes, and newline characters -- all properly escaped per the JSON specification.

**Plans**: 2 plans in 2 waves

Plans:

- [x] 04-01-PLAN.md — JsonFormatter: custom spdlog::formatter subclass with boost::json NDJSON output (TDD, Wave 1)
- [ ] 04-02-PLAN.md — CLI flag wiring (--log-json), config chain, setup.cpp integration, NDJSON retention (Wave 2)

## Progress

**Execution Order:**
Phases execute in numeric order: 1 -> 2 -> 3 -> 4

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Logging Foundation | 4/4 | Complete    | 2026-05-22 |
| 2. File Management + Runtime Observability | 4/4 | Complete | 2026-05-23 |
| 3. Forensics | 3/3 | Complete   | 2026-05-23 |
| 4. JSON Tooling | 1/2 | In Progress | - |
