# Phase 1: Logging Foundation - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-23
**Phase:** 01-logging-foundation
**Mode:** --auto (autonomous decision selection)
**Areas discussed:** Macro design, Source location format, Logger granularity, Migration strategy, Log pattern, SPDLOG_ACTIVE_LEVEL

---

## Macro Design & Naming

| Option | Description | Selected |
|--------|-------------|----------|
| Custom LOG_INFO/LOG_DEBUG macros | Wrapper around SPDLOG_LOGGER_CALL with module tag + source location injection | ✓ |
| spdlog built-in SPDLOG_INFO/SPDLOG_DEBUG | Direct use of spdlog macros with %@ pattern flag for source location | |
| Mixed approach with ENCRO_ prefix | Custom macros with project-specific prefix namespace | |

**[auto] Selected:** Custom LOG_INFO macros — enables module tag injection at call site and future error context chaining in Phase 3.

---

## Source Location Format

| Option | Description | Selected |
|--------|-------------|----------|
| Inject into message body | `file.cpp:128` baked into the log message at macro expansion time | ✓ |
| spdlog pattern flags (%s:%#) | Source location rendered by pattern_formatter on worker thread | |

**[auto] Selected:** Message body injection — avoids async source_loc dangling pointer risk (PITFALLS.md pitfall #2).

---

## Logger Granularity

| Option | Description | Selected |
|--------|-------------|----------|
| Per-file (DEFINE_LOGGER per .cpp) | ~19 named loggers, finest-grained filtering | ✓ |
| Per-module (grouped by directory) | ~5-6 loggers (video, picture, pack, core, infra) | |

**[auto] Selected:** Per-file — aligns with ROADMAP success criterion #2 and enables v2 per-module log level filtering.

---

## Migration Strategy

| Option | Description | Selected |
|--------|-------------|----------|
| All-at-once (single commit) | Convert all 13 spdlog-using files in one pass | ✓ |
| Incremental (file by file) | Gradual migration with temporary mixed state | |

**[auto] Selected:** All-at-once — mechanical replacement, no logic changes, mixed state is error-prone.

---

## Log Pattern Format

| Option | Description | Selected |
|--------|-------------|----------|
| `[timestamp] [level] [module] [file:line] message` | Full context in every line, spdlog pattern flags for module+level | ✓ |

**[auto] Selected:** Standard spdlog pattern with %n (named logger = module tag) and %s:%# (source location) pattern flags.

---

## SPDLOG_ACTIVE_LEVEL

| Option | Description | Selected |
|--------|-------------|----------|
| Release: INFO, Debug: TRACE | Strip trace/debug at compile time in release builds | ✓ |
| Uniform TRACE across all builds | Maximum log detail always available | |

**[auto] Selected:** Per-build-mode — release builds strip trace/debug for zero overhead, debug builds preserve full detail.

---

## Claude's Discretion

- **Log level preservation:** Existing call sites keep their current log levels during migration — no level adjustments.
- **DEFINE_LOGGER placement:** Top of each .cpp file, after includes, before anonymous namespace.
- **Macro namespacing:** `LOG_INFO` (no ENCRO_ prefix) — shorter, project-specific context is clear.
