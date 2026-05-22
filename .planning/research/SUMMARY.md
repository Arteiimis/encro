# Project Research Summary

**Project:** encro -- CLI media encoding tool logging system enhancement
**Domain:** C++ CLI application production-grade logging (spdlog, async, multithreaded)
**Researched:** 2026-05-23
**Confidence:** HIGH

## Executive Summary

This project enhances the logging system of encro, a batch media encoding CLI tool built in C++ with spdlog. The current logging is functional but barebones: a single file with flat text, no source location, no module tagging, and no structured diagnostics beyond what each developer manually writes. The goal is to make every log line answer three questions -- "where did it come from?", "what is it doing?", and "how long did it take?" -- at a debugging quality level that exceeds ffmpeg, HandBrake, and x264, none of which provide source location, per-run files, or structured error context.

The recommended approach is a phased enhancement using only the existing spdlog + fmt + boost::json dependency stack. No new packages are required. The core technical decisions are: switch from spdlog::debug() functions to SPDLOG_DEBUG() macros for automatic source location capture; replace the single default logger with a named-logger registry where each component gets its own logger sharing common sinks; implement RAII scoped timers and error context stacks that serialize context at the call site (not on the async worker thread); and generate timestamped per-run log files with automatic retention of the last 10 runs. All sink creation and logger registration is centralized in a single setupLogging() function in the new src/logging/ directory.

The key risk is the incompatibility between asynchronous logging and thread-local context storage. spdlog's MDC facility does not work with async loggers because the worker thread has different thread-local storage than the caller. The solution -- baking context into the message string at the call site via the logging macros -- is proven, simple, and already designed in the architecture patterns. Performance regression from the added formatting is unlikely for a CLI tool processing tens to hundreds of files, but the async queue size should be increased from 8192 to 32768 as a precaution, and per-frame progress logging should use trace or debug level only.

## Key Findings

### Recommended Stack

All required capabilities are provided by the existing dependency stack. spdlog v1.15.1 (via xmake-repo, with fmt_external mode) provides async logging, named loggers, rotating sinks, compile-time macros for source location capture, custom formatter subclass support, and a stopwatch utility. fmt v11.1.4 handles message formatting. boost::json v1.87.0 (already a dependency) provides JSON string construction for the optional structured output mode.

**Core technologies:**
- **spdlog v1.15.1+ (async_logger, fmt_external):** All logging infrastructure -- async queue, thread pool, named logger registry, shared sinks, custom formatters, macros. No alternative needed; spdlog is the right choice and already integrated.
- **fmt v11.1.4+:** Required by spdlog's fmt_external mode. Already used throughout the codebase. Version pairing with spdlog must be consistent (v1.15.1 pairs with v11.1.4).
- **boost::json v1.87.0:** JSON string construction for --log-json structured output. Already a dependency; avoids adding another JSON library.

**What to avoid:**
- spdlog MDC (spdlog::mdc::put/get) -- incompatible with async loggers. Context must be serialized into the message at the call site.
- structured_spdlog external library -- outside the "no new logging libraries" constraint. Custom spdlog::formatter subclass achieves the same result.
- daily_file_sink -- produces date-patterned files, not per-invocation files. Manual timestamp naming + basic_file_sink_mt is correct.
- Multiple spdlog thread pools -- a single pool (1 thread, 8192 queue) handles all sinks. Adding threads breaks message ordering.

**Version guidance:** Pin spdlog to v1.15.1 and fmt to v11.1.4 in xmake.lua for reproducibility. Upgrading to spdlog v1.17.0 requires fmt v12.1.0, which may cascade to other fmt consumers.

### Expected Features

**Must have for launch (P1):**
- **LOG-01: Source location in every log line** (file.cpp:128) -- the single highest-impact change. Requires macro migration from spdlog::debug() to SPDLOG_DEBUG() across all 19 source files.
- **LOG-09 + LOG-02: Hierarchical module tag convention + implementation** -- video.encode, pack.zip, core.scan. Implemented via logger-per-module with %n pattern flag, tag constants in src/logging/log_tags.h.
- **LOG-03: Stage-level auto-timing** -- RAII ScopedTimer class. Nestable, exception-safe, configurable minimum-duration threshold. Replaces manual elapsedMs anti-pattern.
- **LOG-06 + LOG-07: Per-run timestamped log files + automatic retention** -- encro_20260523_143052.log with last-10 retention via directory scan at startup.

**Should have, add after validation (P2):**
- **LOG-04: Error context chain traceback** -- thread-local RAII context stack. On error, log appends full causal chain. Makes the log the reproduction, not just the error message.
- **LOG-05: Environment snapshot on error** -- concurrent slot states, pending counts, FFmpeg process info captured at error time. Answers "what else was happening?"

**Defer to v2+ (P3):**
- **LOG-08: JSON structured output** (--log-json) -- deferred because its value depends on all other features producing structured metadata first. Implementing JSON before source location, tags, timing, and context exist produces sparse output requiring rework.

**Feature dependency chain:**
LOG-01 (source location) is standalone and must come first. LOG-09 feeds into LOG-02 (tag convention to implementation), coupled in the same phase. LOG-02 enables LOG-03 (tagging to timing labels) and LOG-04 (tagging to error context frames). LOG-04 enables LOG-05 (context chain to snapshot). LOG-06 enables LOG-07 (per-run files to cleanup). All of LOG-01 through LOG-05 together enable LOG-08 (structured data to JSON serialization).

### Architecture Approach

The enhancement introduces a new src/logging/ subsystem that centralizes all logging infrastructure. The architecture is layered: Configuration (LogConfig struct + setupLogging() factory), a Facade layer (macros, ScopedTimer, ScopedErrorContext), the spdlog core (async_logger registry with shared sinks), and Storage (per-run files with retention). Business code never creates sinks or calls spdlog registration APIs.

**Major components:**
1. **LogConfig / setupLogging()** -- Central configuration struct and factory. Creates shared sinks (one file, optional console), builds one named async_logger per module tag, registers all in spdlog's global registry. Extracted from current prelude.cpp into src/logging/setup.cpp.
2. **logging.h macros** -- Header-only: DEFINE_LOGGER(name) creates file-scope cached logger pointer; LOG_TRACE/DEBUG/INFO/WARN/ERROR/CRITICAL wraps SPDLOG_LOGGER_* with automatic source location. LOG_ERROR auto-appends accumulated error context.
3. **ScopedTimer** -- RAII class using std::chrono::steady_clock. Logs elapsed time on destruction. Supports minimum-duration threshold and custom log level.
4. **ScopedErrorContext** -- RAII guard pushing {stage, detail} frames onto a thread-local singly-linked list. LOG_ERROR serializes the chain into the message string before the async queue, bypassing spdlog MDC's async limitation.
5. **retainRecentLogs()** -- Directory scan at startup. Sorts encro_*.log files by parsed timestamp (mtime fallback), deletes all beyond retention cap. Runs before new file creation.

**Key architectural decisions:**
- **Shared sinks, not per-logger sinks.** One file sink instance shared across all named loggers prevents interleaving.
- **Context serialized at call site, not in worker thread.** All context injection is done by the macro before the message enters the async queue. This is the only correct approach with async logging.
- **One DEFINE_LOGGER per .cpp file.** Cached static pointer avoids spdlog::get() hash-map lookup on every log call.

### Critical Pitfalls

1. **Using spdlog::debug() (function) instead of SPDLOG_DEBUG() (macro)** -- Functions evaluate __FILE__/__LINE__ inside the wrapper, showing the wrapper's location for every log line. **Prevention:** Use only macros that expand at the call site. Verify with git grep returning zero function-call log sites after migration.

2. **Thread-local context invisible to async worker thread** -- spdlog's MDC and any custom TLS context are set on the caller thread but formatted on the async worker thread, which has different TLS. **Prevention:** All context (module tags, error chain, timing labels) must be resolved and serialized into the message string at the call site, inside the macro, before async queue insertion.

3. **Module tag naming chaos without enforced convention** -- 19 files, each inventing their own tag format. **Prevention:** Single header of constexpr tag constants (log_tags.h). Enforce dot-notation hierarchy. Consider CI test that validates all tags against known constants.

4. **Async queue bottleneck under high log volume** -- The 1-thread pool with blocking overflow (8192 queue) can stall the encoding pipeline. **Prevention:** Increase queue to 32768. Keep per-frame logging at trace/debug level (stripped in release via SPDLOG_ACTIVE_LEVEL). Profile before optimizing.

5. **Crash handler losing per-run file path after shutdown** -- The crash handler calls logger->critical() + logger->flush(), but if spdlog::shutdown() runs first, the logger is dead. **Prevention:** Never call spdlog::shutdown() before crash handler unregistration. Keep stderr fallback. Consider direct std::ofstream append to known log path as defensive fallback.

## Implications for Roadmap

### Phase 1: Logging Infrastructure (Macros + Tag Convention + Config)

**Rationale:** Foundation phase. Macros and tag convention are prerequisites for every other feature. Implementation is entirely header-only and testable in isolation.

**Delivers:**
- src/logging/config.h -- LogConfig struct
- src/logging/log_tags.h -- constexpr tag constants with dot-notation hierarchy
- src/logging/logging.h -- DEFINE_LOGGER(name), LOG_* macros, LOG_ERROR with context-append support
- src/logging/scoped_timer.h -- ScopedTimer RAII class
- src/logging/error_context.h -- ScopedErrorContext RAII class + thread-local linked list

**Features:** LOG-01 (source location macros), LOG-09 (tag naming convention)
**Pitfalls avoided:** Pitfall 1 (function vs macro), Pitfall 8 (tag chaos)
**Research needed:** None -- well-documented spdlog macro patterns, standard RAII patterns

### Phase 2: Sink Setup + Per-Run Files

**Rationale:** Setup and cleanup infrastructure must exist before integration. Creates shared sinks, registers named loggers, handles per-run file naming and retention.

**Delivers:**
- src/logging/setup.h / src/logging/setup.cpp -- setupLogging(LogConfig) factory, shared sinks, named logger registry
- src/logging/cleanup.h / src/logging/cleanup.cpp -- retainRecentLogs() directory scan + deletion
- Per-run timestamped files: encro_YYYYMMDD_HHMMSS.log, retention of last 10 runs

**Features:** LOG-06 (per-run files), LOG-07 (retention cleanup)
**Pitfalls avoided:** Pitfall 6 (rotation + async drain), Pitfall 10 (crash handler + shutdown)
**Research needed:** None -- standard spdlog sink creation patterns

### Phase 3: Source File Migration (19 files)

**Rationale:** Mechanical refactor that activates source location and module tagging. Every .cpp file gets DEFINE_LOGGER("tag") and spdlog::debug() calls replaced with LOG_DEBUG(). Largest change in file count but entirely mechanical.

**Delivers:**
- Modified src/app/prelude.cpp -- delegates to logging::setupLogging()
- Modified src/infra/crash_runtime.cpp -- uses DEFINE_LOGGER("infra.crash")
- Modified all 17 business .cpp files -- each with DEFINE_LOGGER, LOG_* macros

**Features:** LOG-01 (activated), LOG-02 (module tagging implementation)
**Pitfalls avoided:** Pitfall 1 (verification: zero function-call log sites), Pitfall 3 (context resolved at call site), Pitfall 8 (tags from constants)
**Research needed:** None -- mechanical find-and-replace with grep verification

### Phase 4: Scoped Timing Instrumentation

**Rationale:** Low-risk addition since ScopedTimer was built in Phase 1. Leverages module tags for labels and macros for output. Timing data helps diagnose failures before error context exists.

**Delivers:**
- Instrumented pipeline phases: scan, probe, plan, encode, pack
- Nested timing output, minimum-duration threshold on high-frequency operations

**Features:** LOG-03 (stage auto-timing)
**Pitfalls avoided:** Pitfall 4 (clock drift -- steady_clock for durations, system_clock for timestamps; never mix clock domains)
**Research needed:** None -- well-established RAII timer pattern

### Phase 5: Error Context + Environment Snapshot

**Rationale:** Highest-value diagnostic features. Build on module tags (Phase 3) and are complemented by timing data (Phase 4). Most design complexity due to thread-local + async interaction.

**Delivers:**
- ScopedErrorContext instrumentation at operation boundaries (per file, per stage, per attempt)
- LOG_ERROR auto-appends accumulated context chain
- Environment snapshot on error: concurrent slot states, pending counts, FFmpeg process info
- Context chain + snapshot in crash handler output

**Features:** LOG-04 (error context chain), LOG-05 (environment snapshot)
**Pitfalls avoided:** Pitfall 3 (TLS with async -- context serialized in LOG_ERROR at call site), Pitfall 9 (unbounded growth -- RAII-scoped per operation, capped depth)
**Research needed:** MEDIUM -- thread-local context + async serialization validation; environment snapshot API design

### Phase 6: JSON Structured Output (v2+)

**Rationale:** Formatting layer on top of existing structured data. Deferred until all data-producing features exist to avoid sparse initial output and rework.

**Delivers:**
- Custom json_formatter class (subclass of spdlog::formatter)
- Dedicated JSON file sink (encro_YYYYMMDD_HHMMSS.json), NDJSON format
- Proper string escaping (FFmpeg paths, Unicode error messages)

**Features:** LOG-08 (JSON output)
**Pitfalls avoided:** Pitfall 7 (JSON escaping -- custom formatter with proper escape, not pattern-string), Pitfall 4 (separate wall_ts and elapsed_ms fields)
**Research needed:** HIGH -- custom spdlog::formatter subclass API, JSON escaping edge cases, boost::json formatter integration

### Phase Ordering Rationale

- **Phases 1-2 are the foundation.** Macros, tags, and setup are prerequisites for everything else. Nothing works without them.
- **Phase 3 is the big mechanical migration.** Converts entire codebase to new macro system, activating source location and module tags everywhere. Depends on Phase 1 (macros exist) and Phase 2 (loggers registered at startup).
- **Phase 4 adds timing on top of working tags.** Low-risk, immediate diagnostic value. Confirms tag hierarchy correctness before more complex error context is built.
- **Phase 5 adds the most sophisticated features last.** Error context and environment snapshots make encro's logging genuinely better than any competitor but require solid foundation and benefit from timing data already being present.
- **Phase 6 is deferred intentionally.** Build structured data first, then add output format. Avoids rework and ensures JSON is rich from day one.

### Research Flags

**Phases needing deeper research during /gsd:plan-phase --research-phase N:**
- **Phase 5 (Error Context):** Thread-local linked list + async serialization pattern needs validation against actual spdlog version. ScopedErrorContext::formatContext() must be signal-safe for crash handler integration. Environment snapshot API needs design research -- accessing shared state from low-level error sites without threading AppContext through every function.
- **Phase 6 (JSON Output):** Custom spdlog::formatter subclass implementation is sparsely documented. JSON escaping for Windows paths (backslashes in FFmpeg args) and Unicode (CJK error messages from FFmpeg) needs test-driven validation. boost::json integration inside a formatter's format() method needs performance profiling.

**Phases with standard patterns (skip research-phase):**
- **Phase 1 (Infrastructure):** spdlog macro pattern, RAII timer, thread-local linked list -- all well-documented in spdlog wiki, community patterns, and architecture research.
- **Phase 2 (Setup):** Standard spdlog API calls with official documentation.
- **Phase 3 (Migration):** Mechanical refactoring. grep-and-replace patterns documented in architecture research. No design decisions.
- **Phase 4 (Timing):** RAII scoped timer is a solved pattern with dozens of reference implementations (Adobe Lagrange, NetworKit, Google Highway).

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | All technologies already in codebase (spdlog, fmt, boost::json). Verified against Context7, spdlog wiki, xmake-repo versions. |
| Features | HIGH | Priorities based on competitor analysis (ffmpeg, HandBrake, x264), direct codebase inspection, spdlog capability mapping. |
| Architecture | HIGH | All four patterns backed by spdlog official docs and community patterns. Async+TLS interaction has proven solution (serialize at call site). |
| Pitfalls | HIGH | Top pitfalls confirmed by spdlog GitHub issues (MDC+async #3083, source_loc #2867, async ordering #3190). |

**Overall confidence:** HIGH

All four research areas are well-supported by official documentation, GitHub issues, community patterns, and direct codebase analysis. No major unknowns require speculative decisions.

### Gaps to Address

- **spdlog version pinning:** Project resolves spdlog/fmt from xmake-repo without pinning. Pin to v1.15.1/v11.1.4 for reproducibility. Evaluate v1.17.0/v12.1.0 upgrade as separate task.
- **Environment snapshot API design:** LOG-05 requires accessing appctx::AppContext state from low-level error sites. Thread-local component registry vs. explicit parameter threading needs validation during Phase 5 planning.
- **JSON formatter performance:** Custom spdlog::formatter::format() for JSON output needs benchmarking. Plan performance test as part of Phase 6.
- **Unicode log message handling:** FFmpeg CJK error messages need UTF-8 handling in custom formatter. Needs test cases with real CJK output.
- **Config validation at startup:** setupLogging() should validate log directory writability before creating sinks with a user-friendly error message.

## Sources

### Primary (HIGH confidence)
- [Context7: /gabime/spdlog] -- spdlog API reference: macros, source location flags, named logger registry, rotating file sink, custom flag formatter, stopwatch, async logger configuration
- [spdlog Wiki: Setting up JSON logging](https://github.com/gabime/spdlog/wiki/Setting-up-JSON-logging-with-spdlog) -- Pattern-based JSON approach
- [spdlog Wiki: Custom formatting](https://github.com/gabime/spdlog/wiki/Custom-formatting) -- Custom flag formatter interface
- [spdlog Wiki: FAQ](https://github.com/gabime/spdlog/wiki/FAQ) -- Source location requires macro, flush policy
- [spdlog Wiki: Logger registry](https://github.com/gabime/spdlog/wiki/Logger-registry) -- Named logger retrieval, registration
- [spdlog Wiki: Sinks](https://github.com/gabime/spdlog/wiki/Sinks) -- rotating_file_sink_mt, daily_file_sink, base_sink
- [spdlog Wiki: Asynchronous logging](https://github.com/gabime/spdlog/wiki/Asynchronous-logging) -- Async logger API, thread pool configuration
- [spdlog Issue #3083](https://github.com/gabime/spdlog/issues/3083) -- MDC + async incompatibility confirmed
- [spdlog Issue #2867](https://github.com/gabime/spdlog/issues/2867) -- source_loc owning strings API evolution
- [spdlog Issue #3190](https://github.com/gabime/spdlog/issues/3190) -- Multi-thread async ordering issue
- Project source: src/app/prelude.cpp, src/infra/crash_runtime.cpp, src/video/video_batch_execution.cpp, .planning/PROJECT.md

### Secondary (MEDIUM confidence)
- [spdlog Pattern Syntax (DeepWiki)](https://deepwiki.com/gabime/spdlog/3.4.1-pattern-syntax) -- Pattern flag reference
- [spdlog MDC and Context (DeepWiki)](https://deepwiki.com/gabime/spdlog/7.4-mdc-and-context) -- TLS incompatibility documentation
- [spdlog Custom Format Flags (DeepWiki)](https://deepwiki.com/gabime/spdlog/3.4.2-custom-format-flags) -- Custom flag formatter and clone()
- [spdlog File Rotation (DeepWiki)](https://deepwiki.com/gabime/spdlog/6-file-rotation) -- Size-based and time-based rotation
- WebSearch -- spdlog v1.17.0 release, xmake-repo provides v1.15.1 with fmt v11.1.4
- RAII Scoped Timer Patterns: Adobe Lagrange, NetworKit, community patterns
- Error Context Accumulation: Lumiera thread-local DiagnosticContext, Chromium guard pattern
- Competitor analysis: ffmpeg CLI, HandBrake CLI, x264 CLI logging capabilities

### Tertiary (LOW confidence)
- spdlog community discussion #2151 -- Multiple loggers using same file
- spdlog community discussion #2465 -- Per-run timestamped filenames
- spdlog v1.x README / Wiki (Context7 LLM endpoint) -- Supplementary API reference

---
*Research completed: 2026-05-23*
*Ready for roadmap: yes*
