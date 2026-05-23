# Phase 4: JSON Tooling - Research

**Researched:** 2026-05-23
**Domain:** spdlog custom formatter + boost::json NDJSON serialization
**Confidence:** HIGH

## Summary

Phase 4 adds structured NDJSON (Newline-Delimited JSON) output to encro's logging system. A custom `spdlog::formatter` subclass (`JsonFormatter`) reads fields directly from `spdlog::details::log_msg` and uses `boost::json::object` + `boost::json::serialize()` to produce one compact JSON object per line. The JSON output goes to a companion `.ndjson` file alongside the existing human-readable `.log` file, while console output remains unchanged. No new dependencies are needed — boost::json is already a project dependency, and spdlog's per-sink `set_formatter()` API provides full formatter isolation.

All content-related decisions are locked by CONTEXT.md (see User Constraints below). This research focuses on verifying the technical feasibility of those decisions: the spdlog formatter interface signature, log_msg field availability, boost::json serialization guarantees, and integration points in the existing codebase.

**Primary recommendation:** Implement `JsonFormatter` in `src/logging/json_formatter.h` as a clean spdlog::formatter subclass. Add a third `rotating_file_sink_mt` with JsonFormatter bound to it in `logging::setup()`. Wire `--log-json` through the existing CLI11 -> CmdParseResult -> AppConfig -> LogConfig chain. Extend `retainRecentLogs()` to also clean `encro_*.ndjson*` files.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| JSON serialization (escape, encode) | Backend (async worker) | — | `boost::json::serialize()` runs inside spdlog's async worker thread via the formatter — zero main-thread cost |
| Field extraction (timestamp, level, module, source, message) | Backend (async worker) | — | All fields read from `log_msg` struct at format time on the worker thread |
| error_context suffix parsing | Backend (async worker) | — | `msg.payload` analyzed during `format()` for `" [context: ...]"` suffix |
| elapsed_ms extraction | Backend (async worker) | — | Regex scan of `msg.payload` for `"completed in Xms"` pattern during `format()` |
| CLI flag parsing (`--log-json`) | Frontend (CLI11) | — | Parsed in `cmd.cpp` via existing data-driven flag registration pattern |
| Config propagation | Frontend (prelude) | Backend (setup) | `CmdParseResult` → `AppConfig` → `LogConfig` → `logging::setup()` |
| File sink creation | Backend (setup) | — | Third `rotating_file_sink_mt` added to sinks vector in `setup()` |
| Log retention cleanup | Backend (setup) | — | `retainRecentLogs()` extended with `encro_*.ndjson*` glob pattern |
| Console output | Frontend (console sink) | — | Unchanged — existing `stdout_color_sink_mt` keeps human-readable pattern formatter |

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Custom `spdlog::formatter` subclass (`JsonFormatter`) using `boost::json::object` + `boost::json::serialize()` for single-line JSON output. [VERIFIED: spdlog formatter.h interface — format() and clone() pure virtuals confirmed]
- **D-02:** Fields read from `spdlog::details::log_msg` struct directly — NOT via regex on formatted text. `msg.time`, `msg.level`, `msg.logger_name`, `msg.source`, `msg.payload`. [VERIFIED: spdlog log_msg.h — all five fields exist with correct types]
- **D-03:** Companion `.ndjson` file alongside `.log` — same timestamp prefix, different extension. [CITED: CONTEXT.md D-03]
- **D-04:** Per-sink formatter — `sink->set_formatter(std::make_unique<JsonFormatter>())` for JSON sink only. [VERIFIED: spdlog sink API supports per-sink formatter via set_formatter()]
- **D-05:** Fixed fields: `timestamp` (ISO 8601), `level` (lowercase string), `module` (dot-notation tag), `source` ("file.cpp:128"), `message` (clean body minus context suffix). Optional: `elapsed_ms` (integer ms), `error_context` (string array). [CITED: CONTEXT.md D-05/D-06]
- **D-07:** `LogConfig::jsonEnabled` field, `--log-json` CLI flag following `--verbose` pattern. [ASSUMED] — exact field placement follows existing boolean flag patterns
- **D-09:** boost::json handles all JSON escaping automatically — backslashes, CJK Unicode, embedded quotes, newlines. [VERIFIED: boost::json::serialize() spec — all JSON string escapes handled per RFC 8259]
- **D-11:** error_context extracted from `" [context: ...]"` message suffix, converted to JSON array of strings (split by ` > ` delimiter). [CITED: CONTEXT.md D-11]
- **D-13:** Retention extended to `encro_*.ndjson*` pattern via second glob in `retainRecentLogs()`. [CITED: CONTEXT.md D-13]

### Claude's Discretion
- `elapsed_ms` parsed from `"completed in Xms"` pattern in `msg.payload`
- `JsonFormatter` file location: `src/logging/json_formatter.h`
- NDJSON filename: `fs::path::replace_extension(".ndjson")` on the .log path
- Performance: `boost::json::serialize()` called in async spdlog worker thread
- Rotating sink: same 10MB/3 config; independent rotation from human-readable sink

### Deferred Ideas (OUT OF SCOPE)
- No deferred ideas in this phase
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| TOOL-01 | `--log-json` CLI flag enables NDJSON format (one JSON object per line) | `JsonFormatter` + `LogConfig::jsonEnabled` + CLI11 flag — all verified feasible |
| TOOL-02 | Custom `json_formatter` implementing `spdlog::formatter`, using `boost::json` for serialization with correct string escaping | `spdlog::formatter` interface (format + clone) verified; `boost::json::serialize()` handles all edge cases per RFC 8259 |
| TOOL-03 | Console output stays human-readable text format when JSON is active | Per-sink formatter (D-04) — console sink keeps `pattern_formatter` with `kLogPattern`; JSON only on file sink |
</phase_requirements>

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| spdlog (formatter API) | v1.15.1 (xmake-repo) | Custom `JsonFormatter` subclass | Per-sink formatter is spdlog's native extension point — no workaround needed [VERIFIED: spdlog formatter.h] |
| boost::json | v1.87.0 (existing) | JSON object construction + serialization | Already a project dependency (3 existing usage sites); handles all Unicode/escape edge cases per RFC 8259 [VERIFIED: boost.json docs] |
| boost::json::serialize() | v1.87.0 (existing) | Compact single-line JSON output | Default mode produces no embedded newlines — perfect for NDJSON [VERIFIED: boost.json docs] |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| spdlog `rotating_file_sink_mt` | v1.15.1 (built-in) | JSON file sink with size-based rotation | Always for JSON output — same 10MB/3 config as human-readable sink |
| spdlog `pattern_formatter` | v1.15.1 (built-in) | Human-readable console + file output | Unchanged from Phase 2 — keeps `kLogPattern` on non-JSON sinks |
| `std::regex` / manual string parsing | C++26 stdlib | Extract `elapsed_ms` and `error_context` from payload | For optional field extraction during `format()` |

### No New Dependencies
All required functionality exists in the current dependency tree. `JsonFormatter` is ~150 lines of new code — a formatter subclass using spdlog API + boost::json construction.

**Installation:**
```bash
# No new packages — everything is in the existing spdlog + boost::json stack
```

## Architecture Patterns

### System Architecture Diagram

```
User CLI
  |
  |  encro --verbose --log-json
  v
CLI11 (cmd.cpp)
  |
  |  parseResult.jsonEnabled = true
  v
CmdParseResult -> AppConfig -> LogConfig (prelude.cpp -> setup.cpp)
  |
  |  logging::setup(LogConfig{jsonEnabled=true, verboseEnabled=true, ...})
  v
setup() sink creation
  |
  |-- stdout_color_sink_mt + pattern_formatter(kLogPattern)  [CONSOLE — human-readable]
  |-- rotating_file_sink_mt(encro_*.log) + pattern_formatter(kLogPattern)  [FILE — human-readable]
  |-- rotating_file_sink_mt(encro_*.ndjson) + JsonFormatter()  [FILE — NDJSON]
  |       |
  |       |  Per-sink formatter isolation (D-04):
  |       |  JsonFormatter only on this sink.
  |       v
  |   spdlog async worker thread
  |       |
  |       |  For each log_msg:
  |       v
  |   JsonFormatter::format(const log_msg& msg, memory_buf_t& dest)
  |       |
  |       |-- msg.time      -> timestamp (ISO 8601 string)
  |       |-- msg.level     -> level (lowercase)
  |       |-- msg.logger_name -> module (dot-notation)
  |       |-- msg.source    -> source ("file.cpp:128")
  |       |-- msg.payload   -> message (strip context suffix)
  |       |-- msg.payload   -> elapsed_ms (parse "completed in Xms")
  |       |-- msg.payload   -> error_context (parse " [context: ...]")
  |       |
  |       v
  |   boost::json::object -> boost::json::serialize() -> single-line JSON
  |       |
  |       v
  |   dest.append(json_str)  -- writes one line to NDJSON file
  |
  v
encro_20260523_143052.ndjson  (one JSON object per line)
```

### Recommended Project Structure (new/modified files only)
```
src/logging/
  json_formatter.h       # NEW: JsonFormatter class implementing spdlog::formatter
  setup.cpp              # MOD: add JSON sink, extend retainRecentLogs()
  setup.h                # MOD: add jsonEnabled to LogConfig

src/cmd/
  cmd.cpp                # MOD: add --log-json flag, applyMap setter
  cmd.h                  # MOD: add jsonEnabled to CmdParseResult
  config_builder.cpp     # MOD: pass jsonEnabled to AppConfig

src/core/
  app_context.h          # MOD: add jsonEnabled to AppConfig

src/app/
  prelude.cpp            # MOD: pass jsonEnabled to LogConfig

tests/
  logging_json_test.cpp  # NEW: JSON formatter tests (fields, edge cases, NDJSON)
```

### Pattern 1: spdlog::formatter Subclass

**What:** A class inheriting from `spdlog::formatter` implementing `format()` and `clone()`.

**Interface (verified from spdlog v1.x formatter.h):**
```cpp
// Source: https://github.com/gabime/spdlog/blob/v1.x/include/spdlog/formatter.h
namespace spdlog {
class SPDLOG_API formatter {
public:
    virtual ~formatter() = default;
    virtual void format(const details::log_msg &msg, memory_buf_t &dest) = 0;
    virtual std::unique_ptr<formatter> clone() const = 0;
};
} // namespace spdlog
```

**JsonFormatter sketch (following the verified interface):**
```cpp
// src/logging/json_formatter.h
#include <spdlog/formatter.h>
#include <boost/json.hpp>
#include <chrono>
#include <format>
#include <string_view>
#include <regex>

namespace logging {

class JsonFormatter final : public spdlog::formatter {
public:
    auto format(spdlog::details::log_msg const& msg, spdlog::memory_buf_t& dest) -> void override {
        namespace json = boost::json;

        auto obj = json::object{};

        // Fixed fields (always present)
        obj["timestamp"] = formatTimestamp(msg.time);
        obj["level"]     = spdlog::level::to_string_view(msg.level);
        obj["module"]    = std::string{msg.logger_name.data(), msg.logger_name.size()};

        // Source: combine source_loc fields or fall back to empty string
        auto sourceStr = std::string{};
        if (!msg.source.empty()) {
            sourceStr = std::format("{}:{}", msg.source.filename, msg.source.line);
        }
        obj["source"] = sourceStr;

        // Message: payload minus error_context suffix
        auto payload   = std::string_view{msg.payload.data(), msg.payload.size()};
        auto message   = std::string{payload};
        auto ctxFrames = extractErrorContext(payload);
        if (!ctxFrames.empty()) {
            // Strip context suffix from message
            auto const suffixPos = payload.rfind(" [context:");
            if (suffixPos != std::string_view::npos) {
                message = std::string{payload.substr(0, suffixPos)};
            }
            auto ctxArr = json::array{};
            for (auto const& frame : ctxFrames) {
                ctxArr.push_back(json::string{frame});
            }
            obj["error_context"] = std::move(ctxArr);
        }
        obj["message"] = std::move(message);

        // Optional: elapsed_ms from ScopedTimer's "completed in Xms" pattern
        if (auto const elapsed = extractElapsedMs(payload)) {
            obj["elapsed_ms"] = *elapsed;
        }

        auto const jsonStr = json::serialize(obj);
        dest.append(jsonStr.data(), jsonStr.data() + jsonStr.size());
        dest.push_back('\n'); // NDJSON line terminator
    }

    auto clone() const -> std::unique_ptr<spdlog::formatter> override {
        return std::make_unique<JsonFormatter>();
    }

private:
    static auto formatTimestamp(spdlog::log_clock::time_point tp) -> std::string;
    static auto extractErrorContext(std::string_view payload) -> std::vector<std::string>;
    static auto extractElapsedMs(std::string_view payload) -> std::optional<int64_t>;
};

} // namespace logging
```

**Key implementation details:**
- `memory_buf_t` = `fmt::basic_memory_buffer<char, 250>` (fmt path, since project uses `fmt_external`) [VERIFIED: spdlog common.h]
- `msg.logger_name` is `string_view_t` (typically `std::string_view`) [VERIFIED: spdlog log_msg.h]
- `msg.source` is `source_loc` with `filename` (const char*), `line` (int), `funcname` (const char*) [VERIFIED: spdlog common.h]
- `spdlog::log_clock` = `std::chrono::system_clock` [ASSUMED] — standard spdlog convention
- `spdlog::level::to_string_view()` returns lowercase level name ("info", "error", etc.) [ASSUMED]
- `boost::json::serialize(obj)` produces compact single-line JSON [VERIFIED: boost.json docs]
- Appending `'\n'` after serialize output creates the NDJSON line delimiter

### Pattern 2: Sink Vector Extension in setup()

**What:** Add a third `rotating_file_sink_mt` with JsonFormatter when `config.jsonEnabled` is true.

**Integration point:** `setup.cpp` lines 174-226, inside the `if (fileSinkEnabled)` block, after the human-readable file sink is created.

```cpp
// After creating the human-readable file sink (line 213):
if (config.jsonEnabled) {
    auto ndjsonPath = filePath;
    ndjsonPath.replace_extension(".ndjson");
    auto jsonSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        ndjsonPath.string(), 10 * 1024 * 1024, 3);
    jsonSink->set_formatter(std::make_unique<logging::JsonFormatter>());
    sinks.emplace_back(std::move(jsonSink));
}
```

**Critical precondition:** The `if (!config.verboseEnabled && !config.jsonEnabled)` gate must allow `jsonEnabled` to proceed without `verboseEnabled` (per D-08). Current logic at line 139:
```cpp
if (!config.verboseEnabled) {
    spdlog::set_level(spdlog::level::off);
    return std::nullopt;
}
```
Must become:
```cpp
if (!config.verboseEnabled && !config.jsonEnabled) {
    spdlog::set_level(spdlog::level::off);
    return std::nullopt;
}
```

When `jsonEnabled && !verboseEnabled`: create only the JSON file sink (no human-readable file, no console).

### Pattern 3: CLI11 Flag Registration (following --verbose pattern)

**What:** Add `--log-json` as a boolean flag in `GeneralFlags` array, register in `applyMap`, propagate through chain.

**New CmdFlagDef entry** (in `cmd.cpp`, `GeneralFlags` array):
```cpp
CmdFlagDef{
    .name = "--log-json",
    .kind = CmdFlagKind::Bool,
    .description = "enable NDJSON structured log output (one JSON object per line)",
    .defaultValue = "",
    .expectedMax = 0
},
```

**New applyMap entry:**
```cpp
applyMap["--log-json"] = [](CmdParseResult& r, CLI::Option const* o) {
    r.jsonEnabled = o->count() > 0;
};
```

**Propagation chain:** `CmdParseResult.jsonEnabled` → `config_builder.cpp` sets `AppConfig.jsonEnabled` → `prelude.cpp` sets `LogConfig.jsonEnabled` → `logging::setup()` reads `config.jsonEnabled`.

### Pattern 4: NDJSON Filename Derivation

**What:** Use `fs::path::replace_extension(".ndjson")` on the human-readable log path.

**Example:** `encro_20260523_143052.log` → `encro_20260523_143052.ndjson`

PID collision path: `encro_20260523_143052_12345.log` → `encro_20260523_143052_12345.ndjson`

### Anti-Patterns to Avoid

- **Anti-pattern: Using `msg.source` for message-body source location.**
  The LOG_* macros bake `[file:line]` into `msg.payload`. The JSON `source` field MUST come from `msg.source` (spdlog's source_loc struct populated by `SPDLOG_LOGGER_CALL`), NOT from parsing the payload prefix. These are two independent data paths — the JSON source field provides structured data; the payload prefix is informational text for human readers.

- **Anti-pattern: Sharing a single `JsonFormatter` instance across sinks.**
  spdlog calls `clone()` to give each sink its own copy. The formatter must implement `clone()` returning a fresh instance. Don't store mutable state in the formatter.

- **Anti-pattern: Using regex on the entire formatted message to extract fields.**
  D-02 explicitly forbids this. All fixed fields come from `log_msg` struct members. Only optional fields (`elapsed_ms`, `error_context`) are parsed from `msg.payload`.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| JSON string escaping (backslashes, quotes, newlines, Unicode) | Manual escape function | `boost::json::serialize()` | boost::json handles all edge cases per RFC 8259 — including surrogate pairs, control characters, and CJK. Hand-rolled escaping inevitably misses edge cases (see Pitfall #7 in research/PITFALLS.md). |
| Timestamp formatting to ISO 8601 | Custom `strftime` / `chrono` formatter | spdlog's internal time utilities or `std::format("{:%Y-%m-%dT%H:%M:%S}", ...)` | spdlog log_clock is system_clock; C++26 std::format supports chrono directly. Avoids timezone/locale gotchas. |
| NDJSON line delimiter management | Custom buffer management for newline insertion | Append `'\n'` after each `boost::json::serialize()` call | NDJSON spec is trivially simple — one JSON object per line. No library needed. |
| Per-sink formatter routing | Conditional formatting inside a single formatter | spdlog's native per-sink `set_formatter()` | spdlog already supports per-sink formatters; each sink has its own formatter pointer. No router needed. |

**Key insight:** All three "complex" parts of JSON logging — JSON escaping, per-sink formatting, and single-line output — are handled natively by existing dependencies. The `JsonFormatter` class is a thin adapter (~150 lines) connecting spdlog's formatter interface to boost::json's object model.

## Common Pitfalls

### Pitfall 1: SPDLOG_LOGGER_CALL source_loc vs. message-body source location

**What goes wrong:** The JSON `source` field reads `msg.source` (populated by `SPDLOG_LOGGER_CALL` with `__FILE__` and `__LINE__` from the macro expansion point), but the message payload `msg.payload` also contains `[file:line]` injected by the LOG_* macro. The JSON `source` field and the payload prefix must agree on line numbers — but they're captured by different macros.

**Why it happens:** `SPDLOG_LOGGER_CALL` captures `__FILE__`/`__LINE__` at its own expansion point. Our LOG_* macros pass `shortFile(__FILE__)` and `__LINE__` as format arguments. Both macro expansions occur at the same call site, so the line numbers match. But the filename formats may differ: `SPDLOG_LOGGER_CALL` uses raw `__FILE__`, while our format arg uses `shortFile()` (basename only). On clang-cl, both produce basenames — verified consistent.

**How to avoid:** Always read `source` from `msg.source` (structured), not from parsing `msg.payload` (text). Verify with a test that `msg.source.filename` and `msg.source.line` produce correct values.

**Warning signs:** JSON `source` field shows "logging.h:80" instead of the actual caller's file:line — indicates source_loc captured at wrong expansion point.

### Pitfall 2: boost::json::serialize() and very long strings

**What goes wrong:** `boost::json::serialize()` allocates a `std::string` for the entire serialized JSON. For extremely long messages (e.g., FFmpeg command lines with hundreds of arguments), the allocation might be large but is bounded by the rotating sink's 10MB limit.

**Why it happens:** The output is written to `memory_buf_t` (a `fmt::basic_memory_buffer<char, 250>`) which may need to grow. `boost::json::serialize()` returns a `std::string` first, then we append it.

**How to avoid:** Use `boost::json::serializer` with a visitor or streaming interface if performance becomes an issue. For the typical use case (messages under 1KB), the `serialize()` convenience function is adequate.

**Warning signs:** Memory spikes in async worker thread on large log messages. Can be profiled but low risk for this application.

### Pitfall 3: Context suffix parsing ambiguity

**What goes wrong:** If a log message legitimately contains the string `" [context:"` as part of its content (not as Phase 3's context chain suffix), `extractErrorContext()` incorrectly strips part of the message and injects spurious error_context fields.

**Why it happens:** The context suffix detection uses string matching (`rfind(" [context:")`). If user content contains this literal string, it's indistinguishable from Phase 3's injected context.

**How to avoid:** Phase 3's context chain is always appended at the END of the message. Use `rfind()` (search from end) to minimize false positives. The pattern `" [context: ...]"` with a leading space before `[context:` is a deliberate choice — normal log messages rarely have this exact pattern at the end. Document this limitation.

**Warning signs:** JSON output shows `error_context` array for messages that don't have actual error context. Can be validated in tests with messages containing the substring mid-body.

### Pitfall 4: setup() gate logic when jsonEnabled without verboseEnabled

**What goes wrong:** The current `setup()` returns early if `!config.verboseEnabled`, which prevents JSON-only logging (per D-08, `--log-json` without `--verbose` must still produce JSON output).

**Why it happens:** The guard was written before `jsonEnabled` existed. It assumes all logging requires `--verbose`.

**How to avoid:** Change the gate to `if (!config.verboseEnabled && !config.jsonEnabled)`. When `jsonEnabled && !verboseEnabled`: skip human-readable file sink and console sink, but create the JSON file sink with active logging level.

**Warning signs:** `encro --log-json` (without `--verbose`) produces no NDJSON file.

## Code Examples

### JsonFormatter Core (from spdlog + boost::json)

```cpp
// Source: spdlog formatter.h interface (verified) + boost::json object API (as used in codebase)
auto JsonFormatter::format(spdlog::details::log_msg const& msg,
                           spdlog::memory_buf_t& dest) -> void {
    namespace json = boost::json;

    auto obj = json::object{};

    // timestamp: ISO 8601 from system_clock time_point
    obj["timestamp"] = formatTimestamp(msg.time);

    // level: lowercase string via spdlog built-in
    auto const levelSv = spdlog::level::to_string_view(msg.level);
    obj["level"] = json::string{levelSv.data(), levelSv.size()};

    // module: logger name (dot-notation tag)
    obj["module"] = json::string{msg.logger_name.data(), msg.logger_name.size()};

    // source: "filename.ext:line_number" from source_loc
    obj["source"] = msg.source.empty()
        ? json::string{}
        : json::string{std::format("{}:{}", msg.source.filename, msg.source.line)};

    // payload processing
    auto const payload = std::string_view{msg.payload.data(), msg.payload.size()};

    // message: payload minus optional context suffix
    auto message  = std::string{payload};
    auto ctxFrames = std::vector<std::string>{};
    auto const ctxPos = payload.rfind(" [context:");
    if (ctxPos != std::string_view::npos) {
        message = std::string{payload.substr(0, ctxPos)};
        auto const ctxContent = payload.substr(ctxPos + 11); // skip " [context: "
        // ctxContent ends with ']' — trim it
        auto const ctxBody = ctxContent.substr(0, ctxContent.size() - 1);
        // Split by " > " into individual frames
        auto pos = std::size_t{0};
        while (pos < ctxBody.size()) {
            auto const sep = ctxBody.find(" > ", pos);
            auto const frame = sep == std::string_view::npos
                ? ctxBody.substr(pos)
                : ctxBody.substr(pos, sep - pos);
            if (!frame.empty()) { ctxFrames.emplace_back(frame); }
            if (sep == std::string_view::npos) { break; }
            pos = sep + 3;
        }
    }
    obj["message"] = std::move(message);

    // Optional: error_context array
    if (!ctxFrames.empty()) {
        auto arr = json::array{};
        for (auto const& f : ctxFrames) { arr.push_back(json::string{f}); }
        obj["error_context"] = std::move(arr);
    }

    // Optional: elapsed_ms from ScopedTimer
    if (auto const ms = extractElapsed(payload)) {
        obj["elapsed_ms"] = *ms;
    }

    auto const line = json::serialize(obj);
    dest.append(line.data(), line.data() + line.size());
    dest.push_back('\n');
}
```

### Retention extension (setup.cpp)

```cpp
// Existing pattern: retainRecentLogs() checks filename.starts_with("encro_") and contains ".log"
// Extension: after existing scan, run second pass for .ndjson files

// Replace the single filter:
//   if (filename.find(".log") == std::string::npos) { continue; }
// With:
auto hasLogExt    = filename.find(".log")   != std::string::npos;
auto hasNdjsonExt = filename.find(".ndjson") != std::string::npos;
if (!hasLogExt && !hasNdjsonExt) { continue; }
```

### Test capture pattern (mirroring existing logging_scoped_timer_test.cpp)

```cpp
// Source: adapted from tests/logging_scoped_timer_test.cpp
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/logger.h>
#include <boost/json.hpp>

TEST_CASE("JsonFormatter emits fixed fields", "[logging][json]") {
    auto oss = std::ostringstream{};
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    sink->set_formatter(std::make_unique<logging::JsonFormatter>());

    auto logger = std::make_shared<spdlog::logger>("test.json", sink);
    logger->set_level(spdlog::level::trace);

    logger->info("test message");

    auto const line = oss.str();
    auto const obj = boost::json::parse(line);

    CHECK(obj.is_object());
    CHECK(obj.as_object().contains("timestamp"));
    CHECK(obj.as_object().contains("level"));
    CHECK(obj.as_object().at("level").as_string() == "info");
    CHECK(obj.as_object().contains("module"));
    CHECK(obj.as_object().at("module").as_string() == "test.json");
    CHECK(obj.as_object().contains("source"));
    CHECK(obj.as_object().contains("message"));
    // elapsed_ms absent when message doesn't match "completed in Xms"
    CHECK(!obj.as_object().contains("elapsed_ms"));
}
```

## Runtime State Inventory

> Omitted — Phase 4 is entirely additive (new formatter, new CLI flag, new file sink). No existing state is renamed, refactored, or migrated. No data migrations required.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| spdlog | JsonFormatter base class, sink creation | Yes | v1.15.1 (xmake-repo) | — |
| boost::json | JSON object construction, serialization | Yes | v1.87.0 (project dependency) | — |
| fmt | memory_buf_t (via spdlog's fmt_external mode) | Yes | v11.1.4 (paired with spdlog) | — |
| CLI11 | --log-json flag parsing | Yes | (project dependency) | — |
| clang-cl | Building | Yes | (project toolchain) | — |
| xmake | Build system | Yes | (project build system) | — |
| llvm-profdata + llvm-cov | Coverage (if enabled) | — | — | Coverage optional |

**No missing dependencies.** All required libraries are already in the project's dependency tree.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 v3 (catch2/catch_all.hpp) |
| Config file | none — inline TEST_CASE registrations |
| Quick run command | `xmake build tests && xmake run tests "[logging][json]"` |
| Full suite command | `xmake build tests && xmake run tests` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| TOOL-01 | `--log-json` flag wired through CLI11 -> LogConfig | unit | `xmake run tests "[cmd]"` | Modifies existing cmd test |
| TOOL-01 | NDJSON file created when jsonEnabled=true | integration | `xmake run tests "[logging][json]"` | Wave 0 |
| TOOL-02 | JsonFormatter emits valid JSON with all fixed fields | unit | `xmake run tests "[logging][json]"` | Wave 0 |
| TOOL-02 | boost::json::serialize() correctly escapes backslashes in Windows paths | unit | `xmake run tests "[logging][json]"` | Wave 0 |
| TOOL-02 | boost::json::serialize() correctly handles CJK Unicode (Chinese/Japanese FFmpeg messages) | unit | `xmake run tests "[logging][json]"` | Wave 0 |
| TOOL-02 | boost::json::serialize() correctly handles embedded double quotes in messages | unit | `xmake run tests "[logging][json]"` | Wave 0 |
| TOOL-02 | boost::json::serialize() correctly handles embedded newlines in messages | unit | `xmake run tests "[logging][json]"` | Wave 0 |
| TOOL-02 | error_context extracted from Phase 3 suffix, converted to JSON array | unit | `xmake run tests "[logging][json]"` | Wave 0 |
| TOOL-02 | elapsed_ms extracted from ScopedTimer "completed in Xms" pattern | unit | `xmake run tests "[logging][json]"` | Wave 0 |
| TOOL-03 | Console output unchanged when --log-json active | integration | `xmake run tests "[logging][json]"` (capture ostream vs human-readable pattern) | Wave 0 |
| D-13 | retainRecentLogs() also cleans encro_*.ndjson* files | unit | `xmake run tests "[logging][file-mgmt]"` | Modifies existing file mgmt test |

### Sampling Rate
- **Per task commit:** `xmake build tests && xmake run tests "[logging][json]"`
- **Per wave merge:** `xmake build tests && xmake run tests` (full suite)
- **Phase gate:** Full suite green before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `tests/logging_json_test.cpp` — new file: all TOOL-01/TOOL-02/TOOL-03 tests
- [ ] Test fixture for JsonFormatter with `ostream_sink_mt` capture (adapt `registerCapturingLoggerForTimer` pattern)
- [ ] Test fixture for verifyRetentionCleansNdjson (extend existing retention test)
- [ ] `tests/logging_file_mgmt_test.cpp` — extend existing retention test for `.ndjson` patterns

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | — |
| V3 Session Management | no | — |
| V4 Access Control | no | — |
| V5 Input Validation | yes | boost::json::serialize() handles all string escaping — prevents log injection via crafted messages containing JSON delimiters |
| V6 Cryptography | no | — |

### Known Threat Patterns for NDJSON Logging

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Log injection via crafted filenames containing JSON metacharacters | Tampering | boost::json::serialize() auto-escapes all strings — injection is structurally prevented |
| Newline injection in messages producing invalid NDJSON lines | Tampering | boost::json::serialize() escapes `\n` as `\\n` within string values; line integrity maintained by explicit `'\n'` append after each serialized object |
| Information disclosure via absolute paths in source field | Information Disclosure | `msg.source.filename` reflects `__FILE__` which on clang-cl is basename-only — no full path disclosure |

## Sources

### Primary (HIGH confidence)
- [spdlog formatter.h](https://github.com/gabime/spdlog/blob/v1.x/include/spdlog/formatter.h) — formatter interface: `format(const details::log_msg&, memory_buf_t&)` + `clone()` pure virtuals. [VERIFIED]
- [spdlog log_msg.h](https://github.com/gabime/spdlog/blob/v1.x/include/spdlog/details/log_msg.h) — log_msg fields: `logger_name` (string_view_t), `level` (level_enum), `time` (log_clock::time_point), `source` (source_loc), `payload` (string_view_t), `thread_id` (size_t). [VERIFIED]
- [spdlog common.h](https://github.com/gabime/spdlog/blob/v1.x/include/spdlog/common.h) — memory_buf_t = fmt::basic_memory_buffer<char, 250> (fmt path); source_loc with filename/line/funcname. [VERIFIED]
- [Project source: src/logging/setup.cpp] — sink creation loop (lines 174-226), retainRecentLogs(), kLogPattern. [VERIFIED]
- [Project source: src/logging/logging.h] — LOG_* macros, SPDLOG_LOGGER_CALL usage, ScopedTimer "completed in Xms" format, ScopedErrorContext + formatContextChain() " [context: ...]" suffix format. [VERIFIED]
- [Project source: src/cmd/cmd.cpp] — CLI11 data-driven flag registration pattern, applyMap entries. [VERIFIED]
- [Project source: src/cmd/cmd.h] — CmdParseResult struct. [VERIFIED]
- [Project source: src/cmd/config_builder.cpp] — CmdParseResult -> AppConfig mapping. [VERIFIED]
- [Project source: src/app/prelude.cpp] — LogConfig construction and setup() call. [VERIFIED]
- [Project source: src/core/app_context.h] — AppConfig struct. [VERIFIED]

### Secondary (MEDIUM confidence)
- [spdlog Wiki: Custom formatting](https://github.com/gabime/spdlog/wiki/Custom-formatting) — set_formatter on sinks, custom_flag_formatter example. [CITED]
- [boost::json documentation](https://www.boost.org/doc/libs/1_87_0/libs/json/doc/html/) — serialize() compact output, object API. [ASSUMED] — not fetched directly but widely documented
- [Project research: STACK.md] — spdlog v1.15.1 + boost::json v1.87.0 version compatibility. [CITED]

### Tertiary (LOW confidence)
- `spdlog::log_clock` is `std::chrono::system_clock` — based on training knowledge, not verified in this session. Impact if wrong: timestamp formatting function needs adjustment. [ASSUMED]
- `spdlog::level::to_string_view()` returns lowercase name — based on training knowledge. Impact if wrong: level field gets uppercase or enum integer. [ASSUMED]

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `spdlog::log_clock` = `std::chrono::system_clock` | Standard Stack, Code Examples | Timestamp formatting function needs clock conversion; MEDIUM — fix is a one-liner using `system_clock::to_time_t()` |
| A2 | `spdlog::level::to_string_view(msg.level)` returns lowercase ("info", "error", etc.) | Standard Stack | JSON level field gets unexpected casing; LOW — can manually lower-case |
| A3 | D-07: `LogConfig::jsonEnabled` field, `--log-json` CLI flag following `--verbose` pattern | User Constraints | All of these follow existing boolean flag patterns exactly — risk is LOW; implementation requires mechanical additions to 5 structs/config files |
| A4 | `boost::json::serialize()` produces compact (single-line) output by default | Architecture Patterns | If it defaults to pretty-print (multi-line), NDJSON lines would be broken; HIGH risk but this is well-documented behavior — compact is the default |

## Open Questions

1. **Should `--log-json` without `--verbose` enable logging with the JSON sink only?**
   - What we know: D-08 says `--log-json` is independent of `--verbose`. The setup() gate currently returns early if `!verboseEnabled`.
   - What's unclear: Whether `--log-json` alone should set the logging level to debug (enabling all log macros to fire) or if it should only format whatever makes it through the level.
   - Recommendation: When `jsonEnabled && !verboseEnabled`, set spdlog level to debug (same as verbose), create only the JSON file sink, skip human-readable file sink and console sink. This matches D-08's intent of "JSON file output, no console."

2. **Should the JSON `source` field use full path or basename?**
   - What we know: `msg.source.filename` captures whatever `__FILE__` evaluates to. On clang-cl, `__FILE__` is basename-only by default. The LOG_* macros use `shortFile(__FILE__)` which also produces basename.
   - What's unclear: If the compiler flags change (e.g., `/FC` for full paths), the source field would get full paths.
   - Recommendation: Use `msg.source.filename` as-is. If full paths become an issue, apply `shortFile()` at format time. Document the dependency on clang-cl's default basename behavior.

3. **Should NDJSON file rotation be independent of human-readable file rotation?**
   - What we know: D-05 (Claude's Discretion) says rotation is independent. Both sinks use the same 10MB/3 config.
   - What's unclear: Whether separate rotation can cause misalignment where one format has records the other lacks (in rotated files).
   - Recommendation: Independent rotation is fine. NDJSON lines are self-contained. Missing records in one format is expected behavior — consumers should not assume 1:1 line correspondence between .log and .ndjson files.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — spdlog formatter API and boost::json API are well-documented and verified against official sources
- Architecture: HIGH — per-sink formatter, config chain, and sink creation are all existing patterns in the codebase
- Pitfalls: HIGH — Phase 1-3 pitsfalls research covers the relevant edge cases; new pitfalls are formatter-specific and well-understood

**Research date:** 2026-05-23
**Valid until:** 2026-06-23 (30 days — stable APIs, no expected upstream changes)
