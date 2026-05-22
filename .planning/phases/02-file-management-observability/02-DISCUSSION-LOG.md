# Phase 2: File Management + Runtime Observability - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-23
**Phase:** 2-File Management + Runtime Observability
**Areas discussed:** Per-run file naming, Retention cleanup, RAII ScopedTimer, Crash handler direct write, Rotating file sink, Stage definitions, Log directory fallback, Signal handler safety

**Mode:** --auto (autonomous decision-making — Claude selected recommended options)

---

## Per-Run File Naming

| Option | Description | Selected |
|--------|-------------|----------|
| Timestamp only | `encro_YYYYMMDD_HHMMSS.log` — simple, ROADMAP-specified | ✓ |
| Timestamp + PID always | Always append PID even without collision | |
| ISO 8601 full precision | Include milliseconds for uniqueness | |
| UUID-based | Random UUID instead of timestamp | |

**[auto] Selected:** Timestamp only (`encro_YYYYMMDD_HHMMSS.log`), second precision. PID suffix only on same-second collision as insurance policy.

---

## Retention Cleanup

| Option | Description | Selected |
|--------|-------------|----------|
| Filename sort, 10 most recent | Lexicographic sort on `encro_*.log*` pattern | ✓ |
| Filesystem mtime sort | Sort by modification time instead of name | |
| Keep last N days instead of N files | Time-based retention instead of count-based | |
| Background thread cleanup | Run cleanup periodically during execution | |

**[auto] Selected:** Filename sort with `encro_*.log*` pattern matching. Run at startup before new file creation (Pitfall #6 prevention). Never delete non-encro files from log directory.

---

## RAII ScopedTimer

| Option | Description | Selected |
|--------|-------------|----------|
| Freeform string_view name, LOG_INFO level | Simple RAII class in logging.h, developer chooses name | ✓ |
| Controlled vocabulary enum | Typed stage names from an enum, preventing typos | |
| Template-based auto-name | Use `__FUNCTION__` or `std::source_location` for automatic naming | |
| DEBUG level for timing | Use LOG_DEBUG instead of LOG_INFO for lower verbosity | |

**[auto] Selected:** Freeform `std::string_view` name at LOG_INFO level. `noexcept` destructor. Uses `steady_clock` for duration. Move-only semantics to prevent double-logging.

---

## Crash Handler Direct File Write

| Option | Description | Selected |
|--------|-------------|----------|
| Direct file append via `currentLogFilePath()` | Module-level path, std::ofstream::app, bypass spdlog | ✓ |
| Keep existing spdlog-only path | Rely on existing tryWriteToLogger() + stderr fallback | |
| Global variable for log path | Expose log file path via extern variable in a header | |
| Named pipe / shared memory | IPC channel from crash context to surviving process | |

**[auto] Selected:** Direct file append as first-priority path — matches spdlog pattern format manually. Fallback chain: direct file → spdlog logger → stderr.

---

## Rotating File Sink

| Option | Description | Selected |
|--------|-------------|----------|
| rotating_file_sink_mt, 10MB, 3 rotated | Per FILE-03 requirement | ✓ |
| basic_file_sink_mt, no rotation | Simpler — CLI runs are minutes, rotation unlikely | |
| daily_file_sink | Rotate at midnight instead of by size | |
| Size-based + count-based hybrid | Vary max_size and max_files based on config | |

**[auto] Selected:** `rotating_file_sink_mt` with 10 MB max, 3 rotated files as specified by FILE-03. Cleanup also handles rotated files via `encro_*.log.*` pattern.

---

## Stage Definitions

| Option | Description | Selected |
|--------|-------------|----------|
| Pipeline-level stages only | scan → probe → encode → pack (video), scan → compress → pack (picture) | ✓ |
| Fine-grained sub-stages | Probe details, per-file encode, individual pack entries | |
| Configurable stages | Let user define custom stages via CLI | |
| Auto-detected from call stack | Infer stages from function names automatically | |

**[auto] Selected:** Pipeline-level stages at function entry points in orchestration code. Coarse enough to be meaningful, fine enough to measure pipeline health.

---

## Log Directory Fallback

| Option | Description | Selected |
|--------|-------------|----------|
| Harden existing chain, stderr-only if all fail | Primary → temp → stderr-only, never block | ✓ |
| Block execution on log dir failure | Fail fast if logging can't initialize | |
| In-memory buffer only | Skip file logging entirely, buffer in RAM | |
| User prompt on failure | Ask user for alternate log directory | |

**[auto] Selected:** Hardened fallback chain. Temp dir as secondary. Stderr-only with terminal warning if both fail. Execution never blocked by log infrastructure (FILE-05).

---

## Signal Handler Safety

| Option | Description | Selected |
|--------|-------------|----------|
| Keep atomic flag pattern | No logging in signal handler, set flag, log from main loop | ✓ |
| Add LOG_WARN to signal handler | Log cancellation directly from handler | |
| Async-safe write() bypass | Use raw POSIX write() in signal handler | |

**[auto] Selected:** Keep existing stopsignal atomic flag pattern. spdlog mutexes in signal handler = deadlock risk (documented integration gotcha).

---

## Claude's Discretion

- ScopedTimer move-only semantics (delete copy, keep move)
- Cleanup runs once at startup only — no mid-run background cleanup
- Queue size stays at 8192 (Phase 2 adds minimal per-run overhead)
- New source files follow Phase 1 DEFINE_LOGGER pattern

## Deferred Ideas

None — discussion stayed within phase scope.
