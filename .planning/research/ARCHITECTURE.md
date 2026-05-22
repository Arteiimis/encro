# Architecture Research

**Domain:** C++ CLI logging system (spdlog-based, async, layered)
**Researched:** 2026-05-23
**Confidence:** HIGH

## Standard Architecture

### System Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                     Configuration Layer                              │
│  LogConfig { pattern, level, logDir, retentionCount, ... }          │
│  setupLogging(LogConfig) → registers all named loggers              │
├─────────────────────────────────────────────────────────────────────┤
│                     Logging Facade Layer                             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐              │
│  │ logging.h    │  │ scoped_timer │  │error_context │              │
│  │ (macros)     │  │ (RAII timer) │  │(RAII context)│              │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘              │
│         │                 │                 │                       │
├─────────┴─────────────────┴─────────────────┴───────────────────────┤
│                     spdlog Core (async_logger registry)              │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │  registry: "video.encode" → async_logger                     │   │
│  │            "video.probe"  → async_logger                     │   │
│  │            "pack.zip"     → async_logger                     │   │
│  │            "core.scan"    → async_logger                     │   │
│  │            "app"          → async_logger  (default)          │   │
│  │            ...                                               │   │
│  └──────────────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────────────┤
│                     Sink Layer (shared across all loggers)          │
│  ┌──────────────────┐  ┌──────────────────┐                        │
│  │ file_sink_mt     │  │ stdout_color_sink│                        │
│  │ (per-run .log)   │  │ (--verbose-echo) │                        │
│  └────────┬─────────┘  └────────┬─────────┘                        │
│           │                     │                                   │
├───────────┴─────────────────────┴───────────────────────────────────┤
│                     Storage                                          │
│  %LOCALAPPDATA%/encro/logs/                                          │
│  ├── encro_20260523_143052.log  ← current run                       │
│  ├── encro_20260523_120105.log                                      │
│  ├── encro_20260522_231530.log                                      │
│  └── ... (retention: 10 most recent)                                │
└─────────────────────────────────────────────────────────────────────┘
```

### Component Responsibilities

| Component | Responsibility | Implementation |
|-----------|----------------|----------------|
| `LogConfig` | Owns all logger configuration state — pattern, level, directory, retention count, echo mode | Plain struct in `src/logging/config.h` |
| `setupLogging()` | Creates sinks, builds named loggers, registers them, handles directory creation | Factory function in `src/logging/setup.cpp` (extracted from `prelude.cpp`) |
| `logging.h` macros | Per-file DEFINE_LOGGER + LOG_TRACE/DEBUG/INFO/WARN/ERROR/CRITICAL with automatic source location | Header-only macros wrapping `SPDLOG_LOGGER_*` |
| `ScopedTimer` | RAII start/stop timer that logs elapsed time on destruction | `src/logging/scoped_timer.h` (header-only or compiled) |
| `ScopedErrorContext` | RAII push/pop of thread-local error context; context appended to error-level log lines | `src/logging/error_context.h` |
| `logCleanup()` | Directory scan at startup, delete files exceeding retention count (sort by filename timestamp then mtime) | `src/logging/cleanup.cpp` |
| spdlog registry | Thread-safe singleton owning all named loggers | spdlog built-in (`spdlog::details::registry::instance()`) |

## Recommended Project Structure

```
src/
├── logging/                  # NEW: Logging subsystem
│   ├── config.h              # LogConfig struct (pattern, level, dir, retention)
│   ├── setup.h               # setupLogging() declaration
│   ├── setup.cpp             # setupLogging() implementation — sink creation, logger registration
│   ├── logging.h             # DEFINE_LOGGER + LOG_* macros (header-only)
│   ├── scoped_timer.h        # ScopedTimer RAII class
│   ├── error_context.h       # ScopedErrorContext RAII class + thread-local context stack
│   └── cleanup.h/cleanup.cpp # retainRecentLogs() — directory scan + deletion
├── app/
│   ├── prelude.cpp           # MODIFIED: delegates to logging::setupLogging()
│   ├── prelude.h
│   ├── app_entry.cpp
│   ├── pipeline.cpp          # MODIFIED: uses DEFINE_LOGGER("pipeline"), LOG_* macros
│   └── ...
├── video/
│   ├── video_process.cpp     # MODIFIED: DEFINE_LOGGER("video.process")
│   ├── video_batch_execution.cpp  # MODIFIED: DEFINE_LOGGER("video.encode")
│   ├── video_encode_runner.cpp    # MODIFIED: DEFINE_LOGGER("video.encode.runner")
│   ├── video_info.cpp             # MODIFIED: DEFINE_LOGGER("video.probe")
│   ├── video_output_planning.cpp  # MODIFIED: DEFINE_LOGGER("video.plan")
│   ├── video_progress_parser.cpp  # MODIFIED: DEFINE_LOGGER("video.progress")
│   └── ...
├── pack/
│   ├── pack.cpp               # MODIFIED: DEFINE_LOGGER("pack")
│   ├── pack_service.cpp        # MODIFIED: DEFINE_LOGGER("pack.service")
│   └── packer.cpp              # MODIFIED: DEFINE_LOGGER("pack.zip")
├── picture/
│   ├── picture_process.cpp     # MODIFIED: DEFINE_LOGGER("picture.process")
│   └── picture_compress.cpp    # MODIFIED: DEFINE_LOGGER("picture.compress")
├── core/
│   ├── media_scanner.cpp       # MODIFIED: DEFINE_LOGGER("core.scan")
│   ├── task_executor.cpp       # MODIFIED: DEFINE_LOGGER("core.executor")
│   └── ...
├── infra/
│   ├── crash_runtime.cpp       # MODIFIED: DEFINE_LOGGER("infra.crash")
│   └── ...
└── cmd/
    ├── cmd.cpp                 # MODIFIED: DEFINE_LOGGER("cmd")
    └── ...
```

### Structure Rationale

- **`src/logging/`:** All logging infrastructure in one directory — config, setup, macros, timer, context, cleanup. This is the only place that creates sinks or calls spdlog registration APIs. Business code never sees `create_async`, `init_thread_pool`, or `set_pattern`.
- **Module tag hierarchy mirrors source tree:** `video.encode` lives in `src/video/video_batch_execution.cpp`. Tags use dot-notation (spdlog convention) not `::` (C++ namespace convention) to enable future `spdlog::get("video")` for subtree log-level control.
- **One `DEFINE_LOGGER` per `.cpp` file:** Exactly one static logger variable per translation unit, defined near the top. This is the "per-file" granularity recommended by spdlog documentation.

## Architectural Patterns

### Pattern 1: Named Logger Registry with Shared Sinks (Module Tag Hierarchy)

**What:** Create one `async_logger` per module/component, all sharing the same file sink and console sink. Module tags come from the logger name via `%n` in the pattern. Loggers are registered in spdlog's global registry and retrieved by name.

**When to use:** Any multi-module C++ application using spdlog where different source files need distinct tags in the log output but should write to the same files.

**Trade-offs:**
- **Pro:** Single file sink = single log file per run, no interleaving problems.
- **Pro:** Module tag naturally resolved by spdlog from `log_msg::logger_name` via `%n` pattern flag — no manual prefix strings.
- **Pro:** Per-logger log levels possible (e.g., `spdlog::get("video.encode")->set_level(spdlog::level::trace)` for debug, keep `pack.zip` at `info`).
- **Con:** Registration order matters — all loggers must be registered before any `spdlog::get()` call in .cpp files. Registration must happen in `setupLogging()` before control reaches business code.

**Pattern setup (single place, in setup.cpp):**
```cpp
namespace logging {

auto setupLogging(LogConfig const& cfg) -> void {
    namespace fs = std::filesystem;
    fs::create_directories(cfg.logDir);

    auto logPath = cfg.logDir / fmt::format("encro_{:%Y%m%d_%H%M%S}.log",
        fmt::localtime(std::time(nullptr)));

    // Shared sinks — created ONCE
    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
        logPath.string(), true);
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

    auto sinks = std::vector<spdlog::sink_ptr>{fileSink};
    if (cfg.echoEnabled) { sinks.push_back(consoleSink); }

    // Pattern includes %n for logger name (module tag), %g:%# for source location
    auto pattern = fmt::format(
        "[%Y-%m-%dT%H:%M:%S.%e] [%n] [%^%l%$] [%g:%#] %v");

    // Thread pool — shared across all async loggers
    static auto poolInit = std::once_flag{};
    std::call_once(poolInit, [] {
        spdlog::init_thread_pool(cfg.queueSize, cfg.threadCount);
    });

    auto tp = spdlog::thread_pool();

    // Create and register one logger per module tag
    auto const moduleNames = std::array{
        "app", "cmd",
        "video.encode", "video.encode.runner",
        "video.probe", "video.progress", "video.plan", "video.process",
        "pack", "pack.service", "pack.zip",
        "picture.process", "picture.compress",
        "core.scan", "core.executor",
        "infra.crash",
    };

    for (auto const& name : moduleNames) {
        auto logger = std::make_shared<spdlog::async_logger>(
            name, sinks.begin(), sinks.end(), tp,
            spdlog::async_overflow_policy::block);
        logger->set_pattern(pattern);
        logger->set_level(cfg.level);
        logger->flush_on(spdlog::level::err);
        spdlog::register_logger(logger);
    }

    // Set "app" as default for backward compat with any remaining spdlog::info() calls
    spdlog::set_default_logger(spdlog::get("app"));
}
} // namespace logging
```

**Usage in business code (per .cpp file):**
```cpp
// src/video/video_batch_execution.cpp
#include "logging/logging.h"

DEFINE_LOGGER("video.encode")

void someFunction() {
    LOG_INFO("Encoding batch started: pending={}", vids.size());
    // Output: [2026-05-23T14:30:52.123] [video.encode] [info] [video_batch_execution.cpp:295] Encoding batch started: pending=10
}
```

### Pattern 2: RAII Scoped Timer (Pipeline Phase Timing)

**What:** A non-copyable, non-movable stack object that records `std::chrono::steady_clock::now()` at construction. On destruction (including exception unwind), computes elapsed time and logs it via the appropriate module logger. Supports a minimum-duration threshold to suppress noise and a custom log level.

**When to use:** Every pipeline phase boundary (scan, probe, encode, pack), every significant block scope that matters for debugging. The existing codebase already uses RAII extensively (`CursorGuard`, `EncodingState` lifetime) — this fits the same pattern.

**Trade-offs:**
- **Pro:** Exception-safe — destructor always fires. Zero risk of mismatched start/stop calls.
- **Pro:** Nestable — inner timers produce nested timing output naturally.
- **Con:** The `std::string name_` member allocates (though with SSO for short names, this is negligible). For truly hot paths, use a lower-overhead alternative or conditional compilation.

**Pattern implementation:**
```cpp
// src/logging/scoped_timer.h
#pragma once
#include <spdlog/spdlog.h>
#include <chrono>
#include <string>

namespace logging {

class ScopedTimer {
public:
    ScopedTimer(std::string name,
                spdlog::logger* logger = nullptr,
                spdlog::level::level_enum level = spdlog::level::debug,
                std::chrono::milliseconds minDuration = std::chrono::milliseconds{0})
        : name_(std::move(name))
        , logger_(logger ? logger : spdlog::default_logger_raw())
        , level_(level)
        , minDuration_(minDuration)
        , start_(std::chrono::steady_clock::now())
    {}

    ~ScopedTimer() {
        auto const elapsed = std::chrono::steady_clock::now() - start_;
        if (elapsed < minDuration_) return;
        auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
        logger_->log(
            spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__},
            level_,
            "⏱ {} completed in {} ms", name_, ms.count());
    }

    ScopedTimer(ScopedTimer const&) = delete;
    ScopedTimer& operator=(ScopedTimer const&) = delete;
    ScopedTimer(ScopedTimer&&) = delete;
    ScopedTimer& operator=(ScopedTimer&&) = delete;

private:
    std::string name_;
    spdlog::logger* logger_;
    spdlog::level::level_enum level_;
    std::chrono::milliseconds minDuration_;
    std::chrono::steady_clock::time_point start_;
};

// Convenience macro: uses the file's _encro_logger
#define SCOPED_TIMER(name) \
    logging::ScopedTimer _scoped_timer_##__LINE__(name, _encro_logger)

} // namespace logging
```

**Usage — pipeline phase boundaries:**
```cpp
void handlePathEncoding(appctx::AppContext& ctx, fs::path const& inputPath) {
    SCOPED_TIMER("handlePathEncoding");
    {
        SCOPED_TIMER("scan");
        auto const vids = scanInputVideos(ctx, inputPath);
        if (vids.empty()) { ... }
    }
    {
        SCOPED_TIMER("plan");
        auto const plannedOutputFilesRes = planVideoOutputFiles(...);
    }
    {
        SCOPED_TIMER("encode");
        auto const runRes = videobatch::runEncodingTasks(...);
    }
    // Output (nested):
    // [video.process] [debug] ⏱ scan completed in 234 ms
    // [video.process] [debug] ⏱ plan completed in 12 ms
    // [video.encode]  [debug] ⏱ encode completed in 5432 ms
    // [video.process] [debug] ⏱ handlePathEncoding completed in 5680 ms
}
```

### Pattern 3: Error Context Stack via Thread-Local Linked List (Error Breadcrumbs)

**What:** A RAII guard that pushes an `{operation, detail}` frame onto a thread-local singly-linked list on construction and pops it on destruction. When an error-level log message is emitted, a custom error macro appends the entire accumulated context chain to the log message before it enters the async queue. This avoids spdlog MDC's limitation with async loggers (MDC data is thread-local to the caller thread, invisible to the async worker thread). By serializing context into the message string before queuing, we bypass the async/MDC incompatibility entirely.

**When to use:** Operations that may fail deep in the call stack and need to tell the operator "what was happening" at the time of failure. The existing codebase already tracks: which file, which pipeline stage, which retry attempt. This formalizes that into automatic context accumulation.

**Trade-offs:**
- **Pro:** Works with async loggers (context serialized before queue insertion). spdlog's built-in MDC does not.
- **Pro:** Extremely lightweight on the happy path — one pointer write on push, one on pop, one string copy. No allocations outside the context string.
- **Pro:** Compilable away in release builds via `#ifndef NDEBUG` or a dedicated build flag.
- **Con:** Only visible in error/critical log lines (by design — this is a feature, not a bug). Debug/info lines do not carry the accumulated context unless you explicitly extend the macro.
- **Con:** Thread-local means context does not cross thread boundaries. Encoding tasks dispatched to BS::thread_pool workers must set their own context on the worker thread.

**Pattern implementation:**
```cpp
// src/logging/error_context.h
#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <optional>

namespace logging {

// A single frame in the error context chain
struct ErrorContextFrame {
    std::string stage;     // e.g., "probe", "encode", "pack"
    std::string detail;    // e.g., "file=video.mp4 attempt=2"
    ErrorContextFrame const* parent{nullptr};  // linked list
};

class ScopedErrorContext {
public:
    ScopedErrorContext(std::string stage, std::string detail = {})
        : frame_{std::move(stage), std::move(detail), currentHead()} {
        setHead(&frame_);
    }

    ~ScopedErrorContext() {
        setHead(frame_.parent);
    }

    ScopedErrorContext(ScopedErrorContext const&) = delete;
    ScopedErrorContext& operator=(ScopedErrorContext const&) = delete;
    ScopedErrorContext(ScopedErrorContext&&) = delete;
    ScopedErrorContext& operator=(ScopedErrorContext&&) = delete;

    // Walk the chain and produce a formatted breadcrumb string
    static auto formatContext() -> std::string {
        auto result = std::string{};
        for (auto const* f = currentHead(); f != nullptr; f = f->parent) {
            if (!result.empty()) result += " ← ";
            result += f->stage;
            if (!f->detail.empty()) {
                result += "(";
                result += f->detail;
                result += ")";
            }
        }
        // Reverse so oldest context appears first
        // (collected in reverse order due to linked-list traversal)
        return result;
    }

private:
    static auto currentHead() -> ErrorContextFrame const*;
    static void setHead(ErrorContextFrame const*);

    ErrorContextFrame frame_;
};

} // namespace logging
```

**Integration into LOG_ERROR macro:**
```cpp
// In logging/logging.h — the LOG_ERROR macro appends accumulated context
#define LOG_ERROR(...) do { \
    auto const _ctx = ::logging::ScopedErrorContext::formatContext(); \
    if (_ctx.empty()) { \
        SPDLOG_LOGGER_ERROR(_encro_logger, __VA_ARGS__); \
    } else { \
        SPDLOG_LOGGER_ERROR(_encro_logger, "{} | context: {}", \
            fmt::format(__VA_ARGS__), _ctx); \
    } \
} while(0)
```

**Usage in pipeline code:**
```cpp
auto runEncodingTask(EncodingExecutionContext& execCtx, std::size_t taskIdx,
                     fs::path const& vidPath, std::size_t slot) -> eh::Result<void> {
    ScopedErrorContext ctx("encode", vidPath.filename().string());
    LOG_DEBUG("start encoding: {}", vidPath.string());

    for (int attempt = 1; attempt <= 3; ++attempt) {
        ScopedErrorContext retryCtx("attempt", std::to_string(attempt));
        auto result = encodeVideo(...);
        if (result) return {};
        LOG_WARN("encode attempt {} failed: {}", attempt, result.error());
    }

    LOG_ERROR("all encode attempts exhausted");
    // Output:
    // [video.encode] [error] all encode attempts exhausted | context: encode(video.mp4) ← attempt(1) ← attempt(2) ← attempt(3)
}
```

### Pattern 4: Per-Run Log Files + Directory-Based Retention

**What:** At startup, create a uniquely-named log file using the format `encro_YYYYMMDD_HHMMSS.log`. Before creating the new file, scan the log directory and delete all but the N most recent files (by timestamp parsed from filename, with fallback to filesystem modification time for legacy files).

**When to use:** Every CLI invocation that needs auditability. The user can look at the most recent log, or any of the retained previous N runs.

**Trade-offs:**
- **Pro:** Each run is self-contained — no interleaving, no rotation during a run, trivially archivable.
- **Pro:** Retention count is simple and predictable. User always knows how many runs are preserved.
- **Pro:** Timestamp in filename makes it easy to find the log for a specific run without opening files.
- **Con:** If a run generates a huge log file and there are many small ones, the retention policy is by count, not by total size. For a CLI tool with moderate log output, this is acceptable.
- **Con:** Not using spdlog's built-in `rotating_file_sink` — rotation is based on file size within a single run, while this pattern needs per-run separation. Different use case.

**Pattern implementation:**
```cpp
// src/logging/cleanup.cpp
namespace logging {

auto retainRecentLogs(fs::path const& logDir, std::size_t maxKeep) -> void {
    namespace fs = std::filesystem;
    auto ec = std::error_code{};
    if (!fs::exists(logDir, ec)) return;

    struct LogEntry {
        fs::path path;
        std::filesystem::file_time_type mtime;
        std::optional<std::chrono::system_clock::time_point> parsedTime;
    };

    auto entries = std::vector<LogEntry>{};
    for (auto const& entry : fs::directory_iterator(logDir, ec)) {
        if (!entry.is_regular_file()) continue;
        auto const& p = entry.path();
        if (p.extension() != ".log") continue;

        auto stem = p.stem().string();
        // Parse encro_YYYYMMDD_HHMMSS → std::chrono::time_point
        // Fallback to .mtime if parsing fails (legacy files)
        entries.push_back(LogEntry{
            p,
            entry.last_write_time(),
            parseTimestampFromFilename(stem)  // extract YYYYMMDD_HHMMSS
        });
    }

    // Sort: parsed timestamp (most recent first), then mtime
    std::ranges::sort(entries, [](auto const& a, auto const& b) {
        if (a.parsedTime && b.parsedTime)
            return *a.parsedTime > *b.parsedTime;
        if (a.parsedTime) return true;
        if (b.parsedTime) return false;
        return a.mtime > b.mtime;
    });

    // Delete entries beyond maxKeep
    for (std::size_t i = maxKeep; i < entries.size(); ++i) {
        fs::remove(entries[i].path, ec);
    }
}

} // namespace logging
```

**Called from setupLogging():**
```cpp
auto setupLogging(LogConfig const& cfg) -> void {
    // 1. Create directory if needed
    fs::create_directories(cfg.logDir);

    // 2. Clean up old logs BEFORE creating the new one
    retainRecentLogs(cfg.logDir, cfg.retentionCount);

    // 3. Create the per-run log file with unique timestamp
    auto const now = std::chrono::system_clock::now();
    auto const timeT = std::chrono::system_clock::to_time_t(now);
    auto logFilename = fmt::format("encro_{:%Y%m%d_%H%M%S}.log",
        *std::localtime(&timeT));
    auto logPath = cfg.logDir / logFilename;

    // 4. Create sinks using this path...
}
```

## Data Flow

### Normal Log Message Flow

```
Business Code                       spdlog Core                    Storage
─────────────                       ───────────                    ───────
LOG_INFO("scan:{}", count)
    │
    ├─ DEFINE_LOGGER resolves _encro_logger → "video.process"
    ├─ SPDLOG_LOGGER_INFO constructs log_msg{
    │      logger_name: "video.process"
    │      level: info
    │      source_loc: {__FILE__, __LINE__, __FUNCTION__}
    │      payload: "scan:42"
    │  }
    │
    └─ → async_logger::log(log_msg)
            │
            ├─ enqueue to thread pool queue (8192 entries)
            │
            └─ worker thread:
                    │
                    ├─ file_sink_mt::log(log_msg)
                    │     └─ pattern: [%Y-%m-%dT%H:%M:%S.%e] [%n] [%^%l%$] [%g:%#] %v
                    │     └─ writes: [2026-05-23T14:30:52.123] [video.process] [info] [video_process.cpp:169] scan:42
                    │
                    └─ (if verbose-echo) stdout_color_sink_mt::log(log_msg)
                          └─ same pattern → console
```

### Error Log Message Flow (with Context Accumulation)

```
Business Code                               spdlog Core                    Storage
─────────────                               ───────────                    ───────
{
  ScopedErrorContext ctx1("scan", "dir=/videos");
    {
      ScopedErrorContext ctx2("probe", "file=vid.mp4");
        // thread_local head: ctx2 → ctx1 → nullptr
        LOG_ERROR("ffprobe failed: {}", reason);
          │
          ├─ formatContext() walks linked list: "scan(dir=/videos) ← probe(file=vid.mp4)"
          ├─ Message becomes: "ffprobe failed: timeout | context: scan(dir=/videos) ← probe(file=vid.mp4)"
          └─ → async_logger::log(log_msg with enriched payload)
                  └─ worker thread writes to file sink (no MDC dependency)
    } // ctx2 pops
} // ctx1 pops
```

### Startup Flow

```
main() → prelude::initStartup()
    │
    ├─ commandLineInit(argc, argv)
    │
    └─ LogConfig constructed from CmdParseResult
           │
           ├─ .logDir    = resolveCommonLogDir()  // %LOCALAPPDATA%/encro/logs
           ├─ .level     = cmd.verbose ? debug : off
           ├─ .echoEnabled = cmd.verboseEcho
           ├─ .retentionCount = 10
           ├─ .queueSize = 8192
           └─ .threadCount = 1
                │
                └─ logging::setupLogging(cfg)
                       │
                       ├─ retainRecentLogs(cfg.logDir, 10)   // cleanup first
                       ├─ spdlog::init_thread_pool(8192, 1)  // once
                       ├─ Create file sink + optional console sink
                       ├─ For each module name in registry:
                       │     create async_logger(name, sinks, tp)
                       │     set_pattern(...), set_level(...), flush_on(err)
                       │     spdlog::register_logger(logger)
                       └─ spdlog::set_default_logger(spdlog::get("app"))
```

### Crash Handler Flow

```
SEH exception / signal / terminate()
    │
    └─ crash::writeCrashMessage(message)
           │
           ├─ auto* logger = spdlog::default_logger_raw()  // "app" logger
           ├─ logger->critical("{}", message)              // bypasses macro, directly
           ├─ logger->flush()                              // force flush async queue
           └─ if logger unavailable → fwrite to stderr
```

## Scaling Considerations

| Scale | Architecture Adjustments |
|-------|--------------------------|
| ~30 source files (current) | Per-file DEFINE_LOGGER — no scaling issues at all |
| 100+ files, many modules | Consider per-directory logger granularity (e.g., one logger per `src/*/` subdirectory instead of per-file). Consider auto-registration via a central registry initializer list. |
| Very high log volume (100K+ lines/run) | Increase async queue from 8192. Consider `async_overflow_policy::overrun_oldest` instead of `block`. Check `basic_file_sink_mt` buffering — may need periodic flush for very long runs. |
| Multiple processes (parallel encro instances) | Each process gets its own timestamped log file — no contention. Cross-process log correlation via CLI invocation timestamp in filename. |

### Scaling Priorities

1. **First bottleneck (unlikely for CLI tool):** Async queue overflow — if logging is faster than disk writes for extended periods. Mitigated by the 8192-entry queue and the fact that a CLI tool produces bounded log volume per run.
2. **Second bottleneck:** Per-logger creation at startup (~15-20 loggers, each with 2 sinks = 30-40 registration calls). Trivial at this scale.

## Anti-Patterns

### Anti-Pattern 1: Mixing Configuration and Business Logic

**What people do:** Creating sinks, setting patterns, or configuring flush levels inside business logic files (e.g., `video_batch_execution.cpp` doing `spdlog::rotating_logger_mt(...)` directly).

**Why it's wrong:** When logging configuration is scattered, changing the log format, level, or output path requires touching many files. It creates hidden dependencies — a seemingly unrelated file change can break logging. It also makes it impossible to reason about the complete log pipeline from a single place.

**Do this instead:** All sink creation, pattern configuration, level setting, and logger registration happens in exactly one function: `logging::setupLogging()`. Business code only ever calls `DEFINE_LOGGER("name")` and `LOG_*` macros.

### Anti-Pattern 2: Separate File Sinks Per Logger Writing to the Same File

**What people do:** Creating one `basic_file_sink_mt("encro.log")` per module logger, resulting in N file sinks all pointing at `encro.log`.

**Why it's wrong:** spdlog file sinks do not coordinate with each other. Multiple sinks writing to the same file produce garbled, interleaved output — each sink has its own file position and buffer. A message from logger A may be partially overwritten by logger B.

**Do this instead:** Create exactly one file sink instance and share it across all named loggers. This is the core insight of the shared-sink pattern. One writer, one buffer, one file position — no interleaving.

### Anti-Pattern 3: Using spdlog::mdc with async Loggers

**What people do:** Calling `spdlog::mdc::put("key", "val")` on the caller thread, expecting it to appear in log messages written by the async worker thread.

**Why it's wrong:** MDC uses `thread_local` storage. The async worker thread has different thread-local storage than the caller thread. The MDC data set on the caller thread is invisible when the worker formats the message. This is a documented limitation of spdlog (Issue #3083).

**Do this instead:** Serialize context into the message string at the call site (before the async queue), as shown in Pattern 3 (ScopedErrorContext). The LOG_ERROR macro appends context to the message payload, which then survives the async queue unchanged.

### Anti-Pattern 4: Calling spdlog::get() on Every Log Call

**What people do:** `spdlog::get("video.encode")->info("...")` on every log line, paying the hash lookup cost.

**Why it's wrong:** `spdlog::get()` does a mutex-locked hash map lookup in the registry. In hot paths (encoding loop), this is unnecessary overhead. It is also verbose.

**Do this instead:** The `DEFINE_LOGGER("video.encode")` macro creates a `static spdlog::logger*` at file scope. This resolves once (at first log call in that translation unit) and caches the pointer. All subsequent LOG_* calls use the cached pointer — zero lookup cost.

## Integration Points

### External Services

| Service | Integration Pattern | Notes |
|---------|---------------------|-------|
| FFmpeg/FFprobe subprocesses | Subprocess stdout/stderr not captured by spdlog. If needed, read subprocess output and log via LOG_DEBUG. | Not in scope for this milestone |
| Windows Event Log | Not integrated — spdlog does not have a Windows Event Log sink by default. | Out of scope |
| Crash handler (Windows SEH/terminate) | Direct `logger->critical()` + `logger->flush()` call, bypasses macros. Uses `spdlog::default_logger_raw()` — must ensure the default logger ("app") is registered before crash handlers are installed. | Already implemented; ensure registration order correctness |

### Internal Boundaries

| Boundary | Communication | Notes |
|----------|---------------|-------|
| `setupLogging()` → business code | Via spdlog registry: `spdlog::get("name")` | One-way: setup creates, business retrieves |
| Business code → spdlog async queue | Via `SPDLOG_LOGGER_*` macros (enqueue-only) | Non-blocking unless queue full (then `block` policy) |
| Crash handler → spdlog default logger | Direct pointer access via `spdlog::default_logger_raw()` | Must be set before crash handlers installed |
| `ScopedErrorContext` → LOG_ERROR macro | Thread-local linked list + inline `formatContext()` call | Context serialized at call site, not in worker thread |
| `ScopedTimer` → logger | Passed logger pointer at construction (defaults to `_encro_logger`) | Destructor logs on the calling thread (not async) — timer is synchronous measurement |

## Build Order Implications

Based on dependency analysis, the recommended build order for the logging system changes:

```
Phase 1: Infrastructure (no dependencies)
  └─ src/logging/config.h          (LogConfig struct, pure data)
  └─ src/logging/error_context.h   (ScopedErrorContext, only depends on <string>, <string_view>)
  └─ src/logging/scoped_timer.h    (ScopedTimer, depends on spdlog headers only)

Phase 2: Setup (depends on Phase 1 + spdlog)
  └─ src/logging/cleanup.cpp       (retainRecentLogs, depends on <filesystem>)
  └─ src/logging/setup.cpp         (setupLogging, depends on config.h + cleanup + spdlog sinks)

Phase 3: Macros (depends on Phase 1 + spdlog)
  └─ src/logging/logging.h         (DEFINE_LOGGER + LOG_* macros, header-only)

Phase 4: Integration (depends on Phase 3)
  └─ src/app/prelude.cpp           (MODIFIED: calls logging::setupLogging, then spdlog::get for crash handler)
  └─ src/infra/crash_runtime.cpp   (MODIFIED: uses DEFINE_LOGGER("infra.crash") instead of raw spdlog)
  └─ All 19 business .cpp files    (MODIFIED: replace spdlog::info(...) with LOG_INFO(...), add DEFINE_LOGGER)
```

**Phase ordering rationale:**
- Phase 1 (config + pure utilities) has zero dependencies on the rest of the codebase and zero registration side effects. It can be built and tested in isolation.
- Phase 2 (setup + cleanup) depends on Phase 1 structs and spdlog. It owns all the side effects (directory creation, file deletion, thread pool init, logger registration).
- Phase 3 (macros) is header-only and trivially testable by including it and verifying LOG_INFO compiles.
- Phase 4 (integration) is a mechanical refactor across all files. It can be done file-by-file. The old `spdlog::info(...)` calls are replaced with `LOG_INFO(...)`. The only complex file is `setup.cpp` which replaces the current `prelude.cpp` logging code.

## Sources

- spdlog Registry and Logger Management: WebSearch, multiple sources confirming `spdlog::register_logger()`, `spdlog::get()`, shared-sink pattern, and per-logger formatting via `%n` flag
- spdlog Source Location: WebSearch confirming `%g`, `%#`, `%!`, `%@` pattern flags, `spdlog::source_loc` struct, C++20 `std::source_location` support, and `SPDLOG_LOGGER_*` macro capture of `__FILE__`/`__LINE__`/`__FUNCTION__`
- spdlog MDC and Async Limitations: WebSearch confirming `spdlog::mdc::put/get/remove/clear` API introduced in v1.14.0, `%&` pattern flag, documented incompatibility with async loggers (Issue #3083)
- spdlog rotating_file_sink: WebSearch confirming constructor parameters (`max_size`, `max_files`, `rotate_on_open`), numeric cascade rotation, deletion of oldest when `max_files` exceeded, maximum of 200,000 files
- RAII Scoped Timer Patterns: WebSearch across ACTS, Celeritas, Google Highway, Stack Overflow — consistent pattern of `steady_clock::now()` capture, destructor logging, non-copyable, minimum threshold support
- Error Context Accumulation: WebSearch covering Lumiera's thread-local DiagnosticContext linked list, Chromium's relaxed atomic flag guard, cpptrace's search-phase capture — supporting the linked-list with serialization pattern for async compatibility
- Logger Configuration Separation: WebSearch covering KDAB's KDSPDSetup (TOML config → spdlog setup), config struct + factory pattern, dot-notation logger naming convention

---
*Architecture research for: encro logging system enhancement*
*Researched: 2026-05-23*
