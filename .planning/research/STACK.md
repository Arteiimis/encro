# Stack Research — Logging System Enhancement

**Domain:** C++ CLI application production-grade logging (spdlog)
**Researched:** 2026-05-23
**Confidence:** HIGH

## Recommended Stack

### Core Technologies

| Technology | Version | Purpose | Why Recommended |
|------------|---------|---------|-----------------|
| spdlog | v1.15.1+ (xmake-repo), v1.17.0 (upstream latest) | Async logging framework | Already in codebase; mature, header-compiled with `fmt_external`; full feature set for source location, rotation, custom formatters, stopwatch |
| fmt | v11.1.4+ (paired with spdlog v1.15.1) | String formatting | Required by spdlog's `fmt_external` mode; used throughout codebase for `std::format_string`; v11+ required for C++26 compatibility |
| boost (json) | v1.87.0 (existing) | JSON string construction for structured logging | Already a dependency; used for JSON log payload in custom `json_formatter` implementation. Avoids adding nlohmann_json as a new dependency |

### Supporting Libraries

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| spdlog `rotating_file_sink` | built-in | Per-session log rotation by size limit | Always — replaces current `basic_file_sink_mt` for file log. 5-10 MB max, 3 backup files |
| spdlog `stopwatch` | built-in | RAII-compatible elapsed time measurement | Wrapped in custom `ScopedTimer` class for pipeline phase timing |
| spdlog `pattern_formatter` + `custom_flag_formatter` | built-in | Extensible log pattern system | Used to add `%@` (file:line) flag to pattern; custom JSON formatter via `spdlog::formatter` subclass |
| spdlog compile-time macros (`SPDLOG_INFO`, etc.) | built-in | Zero-cost source location capture | Replaces all existing `spdlog::info()` / `spdlog::debug()` calls |

### Development Tools

| Tool | Purpose | Notes |
|------|---------|-------|
| xmake `add_defines()` | Set `SPDLOG_ACTIVE_LEVEL` globally | Define `SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE` in debug/coverage modes, `SPDLOG_LEVEL_INFO` in release to strip debug/trace calls at compile time |

## Installation

No new packages required. All features are built into the existing `spdlog[fmt_external]` and `fmt` packages already declared in `xmake.lua`.

To upgrade to the latest available xmake-repo versions (the project currently resolves against whatever xmake-repo provides):

```lua
-- In xmake.lua, pin versions for reproducibility (optional):
add_requires("spdlog[fmt_external] v1.15.1")
add_requires("fmt v11.1.4")
```

The upgrade path from the current project baseline:
1. Verify current spdlog version supports `%@` flag (all v1.x do) and `custom_flag_formatter` (v1.0+)
2. No API-breaking changes between spdlog v1.13 and v1.17 that affect the features used

## Alternatives Considered

| Recommended | Alternative | When to Use Alternative |
|-------------|-------------|-------------------------|
| spdlog built-in macros (`SPDLOG_INFO`) | `std::source_location` + custom wrapper | If spdlog macros prove too restrictive for the codebase style, but spdlog macros are zero-cost and idiomatic |
| Custom `spdlog::formatter` subclass for JSON | Pattern-based JSON (spdlog wiki approach) | Pattern-based is simpler but requires manual bracket management; custom formatter produces clean NDJSON (one JSON object per line) |
| Named loggers for module tagging (`%n`) | Custom flag formatter with thread-local module tag | Named loggers are spdlog-idiomatic; custom flags add complexity with no benefit here |
| `rotating_file_sink_mt` for session rotation | `daily_file_sink` | `daily_file_sink` creates date-stamped files, not per-invocation files; rotating sink with manual retention cleanup gives per-session control |
| External filesystem cleanup for retention | spdlog built-in rotation (max_files parameter) | spdlog's max_files controls backup count within a sink, not cross-session files; manual cleanup with `std::filesystem` at startup is the correct approach |

## What NOT to Use

| Avoid | Why | Use Instead |
|-------|-----|-------------|
| spdlog MDC (`spdlog::mdc::put/get`) | **Not supported in async mode** (thread-local storage does not transfer to async processing thread). The project uses `spdlog::async_logger` | Pass context data as message arguments; for LOG-04/LOG-05 (operation chain, environment snapshot), construct context strings manually or use a context struct with `fmt::formatter` specialization |
| `structured_spdlog` library | External dependency outside project constraints ("不改变 spdlog 以外的日志库") | Custom `spdlog::formatter` subclass for JSON; same outcome, no new dependency |
| `daily_file_sink` for per-invocation files | Produces date-patterned files, not per-execution files; sessions within the same day share a file | Generate timestamp-based filename manually at startup, use `rotating_file_sink_mt` |
| `basic_file_sink_mt` (current) | No size-based rotation within a session; single large file | `rotating_file_sink_mt` with 10 MB max size |
| `spdlog::info()` / `spdlog::debug()` (current call style) | Does not capture source location; `%@` / `%s` / `%#` / `%!` flags produce empty output | `SPDLOG_INFO()` / `SPDLOG_DEBUG()` macros (capture `__FILE__`, `__LINE__`, `__FUNCTION__` via `spdlog::source_loc`) |
| Multiple separate spdlog thread pools | Each pool adds thread overhead; a single async pool already handles all sinks | Keep single `spdlog::thread_pool` (8192 queue, 1 thread) as currently configured |

## Stack Patterns by Variant

**Default (human-readable log format):**
- Pattern: `"[%Y-%m-%dT%H:%M:%S.%e%z] [%^%l%$] [%n] [%@] %v"`
- Output: `[2026-05-23T14:30:52.123+08:00] [debug] [video.encode] [video_encode_runner.cpp:128] Starting encode slot 3`
- Use for: normal operation, debugging

**JSON / NDJSON (structured log format, `--log-json`):**
- Custom `json_formatter` implementing `spdlog::formatter` interface
- One JSON object per line (NDJSON) — no opening/closing brackets
- Per-line format: `{"ts":"2026-05-23T14:30:52.123+08:00","lvl":"debug","mod":"video.encode","src":"video_encode_runner.cpp:128","msg":"Starting encode slot 3"}`
- Use `boost::json` (already a dependency) for JSON string construction inside the formatter
- Applied to file sink only; console sink stays human-readable

## Version Compatibility

| Package A | Compatible With | Notes |
|-----------|-----------------|-------|
| spdlog v1.15.1 (`fmt_external`) | fmt v11.1.4 | Default xmake-repo pairing as of mid-2025 |
| spdlog v1.17.0 (`fmt_external`) | fmt v12.1.0 | Latest upstream pairing (Jan 2026); xmake-repo may not have this yet |
| spdlog v1.13+ (`%@` flag) | fmt v10.x+ | `%@` (combined file:line) flag available since v1.x; no lower-bound issue |

**Guidance:** The project does not pin spdlog/fmt versions in `xmake.lua`. To guarantee reproducibility, pin to `spdlog v1.15.1` and `fmt v11.1.4` — the versions xmake-repo currently resolves. Upgrading to v1.17.0 would require also upgrading fmt to v12.1.0, which may cascade to other consumers of fmt in the project. Evaluate as a separate task.

## Key Pattern Flags Reference

| Flag | Meaning | Example Output |
|------|---------|----------------|
| `%@` | Source file:line (combined) | `video_encode_runner.cpp:128` |
| `%s` | Source filename (basename only) | `video_encode_runner.cpp` |
| `%!` | Function name | `encodeBatch` |
| `%#` | Line number | `128` |
| `%n` | Logger name (component tag) | `video.encode` |
| `%l` | Full level name | `debug`, `info`, `warn`, `error` |
| `%L` | Short level name | `D`, `I`, `W`, `E` |
| `%t` | Thread ID | `1234` |
| `%P` | Process ID | `5678` |
| `%Y-%m-%dT%H:%M:%S.%e%z` | ISO 8601 timestamp | `2026-05-23T14:30:52.123+08:00` |
| `%^...%$` | Color range (console only) | Enables color for level name |

## Sources

- [Context7: /gabime/spdlog] — Compile-time macros (`SPDLOG_INFO`, `SPDLOG_ACTIVE_LEVEL`), source location flags (`%@`, `%s`, `%!`, `%#`), named logger registry, rotating file sink, custom flag formatter, stopwatch, formatting flags reference, flush policy, async logger configuration
- [spdlog Wiki: Setting up JSON logging](https://github.com/gabime/spdlog/wiki/Setting-up-JSON-logging-with-spdlog) — Pattern-based JSON approach (reference; custom formatter class is preferred)
- [spdlog Wiki: Custom formatting](https://github.com/gabime/spdlog/wiki/Custom-formatting) — Custom flag formatter interface and `set_formatter` API
- [spdlog Wiki: FAQ](https://github.com/gabime/spdlog/wiki/FAQ) — Source location requires macro usage (not `logger->info()`), flush policy
- [spdlog Wiki: Logger registry](https://github.com/gabime/spdlog/wiki/Logger-registry) — Named logger retrieval, registration, dropping
- [spdlog Wiki: Sinks](https://github.com/gabime/spdlog/wiki/Sinks) — `rotating_file_sink_mt`, `daily_file_sink`, `base_sink` for custom sinks
- WebSearch — spdlog latest release v1.17.0 (January 4, 2026), xmake-repo current provides v1.15.1 with fmt v11.1.4 — MEDIUM confidence
- WebSearch — spdlog MDC not supported in async mode (thread-local vs async processing thread) — HIGH confidence, confirmed by Context7 docs

---
*Stack research for: encro logging system enhancement*
*Researched: 2026-05-23*
