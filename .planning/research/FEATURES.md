# Feature Research

**Domain:** C++ CLI media encoding tool debug logging (spdlog-based)
**Researched:** 2026-05-23
**Confidence:** HIGH

## Feature Landscape

### Table Stakes (Users Expect These)

Features users assume exist. Missing these = product feels incomplete. For a debug-oriented verbose log in a CLI tool, "users" are the developers debugging issues, not end-users running the tool.

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Source location in every log line (`file.cpp:128`) | Without it, `grep`-ing a large log for "what logged this?" is guesswork. Every production logging framework provides this (spdlog macros do it natively via `%s:%#`). | LOW | Only works when switching from `spdlog::debug()` to `SPDLOG_DEBUG()` macros. The macro injects `__FILE__`, `__LINE__`, `SPDLOG_FUNCTION` into `source_loc`. Pattern flag `%@` or `%s:%#` renders it. |
| Module/component tagging (`[video.encode]`, `[pack.zip]`) | A 30-file codebase without module tags produces undifferentiated log lines. Knowing *which subsystem* emitted a debug line is the first question in any investigation. | MEDIUM | Requires custom flag formatter or logger-per-module with name prefix. spdlog logger names (`%n`) provide this natively when each module gets its own logger, but 19 source files all use the default logger currently. Migration cost is moderate. |
| Per-log-level verbosity control | Already exists via `--verbose` (enables debug) and spdlog level hierarchy. The toggle between `off` and `debug` is table stakes. | LOW | Already implemented in `prelude.cpp:66-75`. |
| Timestamp with millisecond precision | Already exists in the pattern `[%Y-%m-%dT%H:%M:%S.%e%z]`. Table stakes for correlating logs with external events and measuring durations. | LOW | Already implemented. ISO 8601 format is the right choice. |
| Async file I/O (no blocking the pipeline) | File writes must never stall the encoding pipeline. spdlog's `async_logger` with a thread pool already provides this. | LOW | Already implemented in `prelude.cpp:105-113`. Queue size 8192, single thread, block-on-full policy is appropriate for a CLI tool that generates limited log volume. |
| Thread safety (no interleaved lines) | Parallel encoding with `BS::thread_pool` generates concurrent log calls. The async logger's single consumer thread guarantees line-level atomicity. | LOW | Already implemented. The single-thread async pool serializes output, preventing interleaved lines. |
| Flush on critical/fatal | Crash handler calls `logger->critical()` + `logger->flush()`. Without flush-on-error, the last lines before a crash are lost. | LOW | Already implemented in `crash_runtime.cpp:34-36` and `prelude.cpp:117` (`flush_on(spdlog::level::err)`). |
| Log-to-file | Exists. Single file `encro.verbose.log` in `%LOCALAPPDATA%/encro/logs/`. Table stakes. | LOW | Already implemented. Upgrade to per-run files is a differentiator (LOG-06). |

### Differentiators (Competitive Advantage)

Features that set the encro logging system apart. These directly serve the Core Value from PROJECT.md: "Every log line answers three questions: where did it come from, what is it doing, how long did it take."

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| Stage-level auto-timing with RAII scoped timers (LOG-03) | Answers "how long did it take?" without manual `elapsedMs` calculations. One line of instrumentation per stage: `ScopedTimer t("video.probe", logger)`. Auto-logs on scope exit (success, failure, exception). The current codebase already manually logs `elapsedMs` in `video_batch_execution.cpp` -- this replaces ad-hoc timing with a systematic, non-forgettable pattern. | LOW | Pure library code. A ~30-line RAII class using `std::chrono::high_resolution_clock`. No spdlog modifications needed. Key design choice: log on destruction with `spdlog::level::info` for normal completion, allow a `setFailed()` method to log at `warn` level for early-exit stages. |
| Error context with operation chain traceback (LOG-04) | When a video encode fails, the log shows the full causal chain: `Processing file "vacation.mp4" → stage: encode → attempt 2/3 → FFmpeg exited with code 1: "Invalid codec"`. This eliminates the need to reproduce the error -- the log *is* the reproduction. | MEDIUM | Requires a thread-local or task-local context stack (`std::vector<ContextFrame>` pushed/popped via RAII). Each frame captures file path, stage name, attempt number. On error, the stack is serialized into the log message. Must be exception-safe (stack unwinds correctly). |
| Environment snapshot on error (LOG-05) | On failure, the log captures: concurrent slot states (which files were being processed in parallel), pending/remaining file counts, FFmpeg process info (PID, arguments). This answers "what else was happening?" -- the most common question when parallel encoding fails nondeterministically. | HIGH | Requires access to shared state at error time. The `appctx::AppContext` already holds most of this information. The challenge is accessing it from low-level error sites without threading it through every function signature. A thread-local registry that components register their state into may be cleanest. |
| Per-run log files with timestamp naming (LOG-06) | Currently all runs share `encro.verbose.log` -- you can't compare two runs without manually renaming the file. Per-run files like `encro_20260523_143052.log` are the industry default (every tool from nginx to Docker does this). | LOW | Generate timestamped filename at startup in `setupLogging()`. Pass to `basic_file_sink_mt`. No spdlog modifications needed -- just string formatting with `std::put_time`. |
| Automatic log retention (last 10 runs) (LOG-07) | Without retention, log files accumulate indefinitely in `%LOCALAPPDATA%`. With it, the user doesn't need to think about cleanup -- the system self-manages. 10 runs is ~10-50 MB typical for a media encoding CLI. | LOW | A simple directory scan on startup: list `encro_*.log` files sorted by timestamp, delete all but the 10 most recent. Can be done in `setupLogging()` after creating the new file. No dependency on spdlog rotation sinks -- manual cleanup is simpler and gives exact control over count. |
| Optional JSON structured output (LOG-08) | Machine-parseable logs enable CI integration, performance dashboards, and automated error classification. When `--log-json` is passed, each log line becomes a JSON object with typed fields (`"level":"info"`, `"duration_us":1234`, `"module":"video.encode"`). | MEDIUM | spdlog provides `stdout_json_sink_mt` and the `json_formatter` community extension. Two approaches: (1) custom `pattern_formatter` that outputs JSON, or (2) separate sink entirely for JSON path. Approach (1) is simpler: a custom formatter class serializes the log message + all metadata as JSON. Must handle optional fields (source location may be empty if using raw `spdlog::debug()` calls). |
| Hierarchical module tag naming convention (LOG-09) | Flat tags (`video`, `pack`, `picture`) lose subsystem context. Hierarchical tags (`video.encode`, `video.probe`, `pack.zip`, `picture.compress`) enable `grep` filtering by granularity: `grep 'video\.'` for all video operations, `grep 'video\.encode'` for just encoding. | LOW | Design decision, not code. Define the tag hierarchy document in `src/app/` or `.planning/` so all contributors use consistent names. Enforcement via code review. Could add a compile-time check that all used tags match the registry, but that's over-engineering for 30 source files. |
| Operation chain + environment snapshot combined on crash | The crash handler (`crash_runtime.cpp`) currently logs stacktrace + exception. Adding the operation chain and environment snapshot to the crash report makes crash forensics dramatically more useful: you see both *where* the code crashed AND *what* it was trying to do. | MEDIUM | Extend `writeCrashReport()` to query the operation chain context stack and environment snapshot registry before writing. Both must be signal-safe (no heap allocation, no locks). Since the data is pre-formatted into thread-local storage, reading it in the crash handler is safe. |

### Anti-Features (Commonly Requested, Often Problematic)

Features that seem good but create problems for this type of tool.

| Feature | Why Requested | Why Problematic | Alternative |
|---------|---------------|-----------------|-------------|
| Dynamic runtime log level reconfiguration (SIGUSR1 / config file reload) | "I want to turn on debug logging without restarting." | A CLI encoding tool runs for minutes, not days. If debugging is needed, re-run with `--verbose`. The infrastructure (signal handling, file watching, thread-safe level mutation) outweighs the value for a short-lived process. The complexity of making spdlog's level atomic at runtime with async logger is significant and error-prone. | Re-run the tool with `--verbose`. The per-run log file naming (LOG-06) means you can compare runs. |
| Log sampling / rate limiting (`LOG_EVERY_N`) | "I don't want to flood the log with per-frame stats." | A CLI tool generates finite output for a bounded task. Sampling hides data that might be the exact clue needed for debugging. If the log is too noisy, the fix is to reduce *what* is logged at that level, not to randomly drop entries. Sampling makes debugging nondeterministic -- the worst property for a debug log. | Use log levels correctly: frequent per-frame data at `trace`, workflow events at `debug`/`info`. The verbosity hierarchy IS the rate control. |
| Log compression (gzip old logs) | "Log files get large over time." | Adds a dependency (zlib or manual gzip integration), complicates log viewing (must decompress before reading), and the retention cap of 10 files (LOG-07) already bounds total disk usage to ~10-50 MB for text logs. For a CLI tool, this is negligible. | The 10-file retention cap (LOG-07) is sufficient. If logs become large, fix what's being logged, not the storage format. |
| Real-time WebSocket/UI dashboard for log viewing | "I want to watch logs in a browser." | Explicitly out of scope per PROJECT.md. Would require a web server, WebSocket library, HTML/JS frontend. Adds hundreds of lines of non-core code and multiple new dependencies for a feature that duplicates `tail -f` and `jq` (for JSON output). | `--verbose-echo` already streams logs to the terminal. For JSON output (LOG-08), pipe to `jq` or any JSON viewer. |
| Binary/compact log format for performance | "Text formatting is slow at high throughput." | A CLI media encoder processes ~10-100 files, generating O(thousands) of log lines over minutes. The bottleneck is FFmpeg encoding, not log formatting. spdlog's async mode already decouples formatting from the caller. Binary logging (NanoLog-style) requires offline decompilation tools -- terrible DX for a debug log. | JSON output (LOG-08) provides machine readability without sacrificing human readability. Text is the right default. |
| Network/remote log shipping (syslog, Kafka, Loki) | "I want centralized log collection." | Out of scope per PROJECT.md. This is a local-only tool. Network shipping adds dependencies, failure modes (network down = log loss), and configuration complexity (endpoint URLs, auth, retry policies) that are antithetical to a CLI tool's reliability requirement. | The log file is local. Users who need centralization can use file-based collectors (Fluentd, Filebeat) to tail the log directory. This is an operations concern, not a tool concern. |

## Feature Dependencies

```
Stage timing (LOG-03)
    └──enhances──> Module tagging (LOG-02)
                       └──requires──> Naming convention (LOG-09)

Error context chain (LOG-04)
    └──requires──> Module tagging (LOG-02)
                       └──requires──> Naming convention (LOG-09)

Environment snapshot (LOG-05)
    └──requires──> Error context chain (LOG-04)
                       (snapshot is only useful when you know the operation chain)

Per-run log files (LOG-06)
    └──enables──> Log retention cleanup (LOG-07)

JSON output (LOG-08)
    └──enhances──> All other features
    (JSON captures source location, module, timing, error context as typed fields)
    Note: JSON can be implemented independently, but its value increases with each
    structured field available to serialize.

Source location (LOG-01)
    └──requires──> Migration from spdlog::debug() to SPDLOG_DEBUG() macros
    (Standalone -- no dependency on other features, but enables richer all-feature output)

Module tagging (LOG-02)
    └──requires──> Logger-per-module OR custom flag formatter
    (Standalone architectural decision -- independent of source location migration)
```

### Dependency Notes

- **LOG-02 requires LOG-09:** You can't implement module tagging without first defining the tag naming convention. These should be implemented in the same phase.
- **LOG-04 requires LOG-02:** The operation chain displays module tags in each frame (`stage: video.encode`). Without module tagging, the chain is less useful (just shows function names or ad-hoc strings).
- **LOG-05 requires LOG-04:** The environment snapshot is supplementary to the operation chain. A snapshot without the chain context ("which operation was running when this snapshot was taken?") loses most of its diagnostic value.
- **LOG-03 enhances LOG-02:** Scoped timers should use the same module tag system for their labels (`ScopedTimer("video.encode")`), creating a unified naming scheme.
- **LOG-06 enables LOG-07:** Retention cleanup operates on per-run files. Until files are per-run, there's nothing to clean up.
- **LOG-08 enhances all:** JSON output is a formatting layer. Once source location, module tags, timings, and error context exist in the log metadata, JSON serializes them as typed fields automatically. Implementing JSON before these features means the JSON output would be sparse (missing source location, module, duration fields).

## MVP Definition

### Launch With (v1)

Minimum viable product -- what's needed to validate the concept and make the log useful for debugging.

- [ ] **LOG-01 (Source location):** Switch all `spdlog::debug/info/warn/error()` calls to `SPDLOG_DEBUG/INFO/WARN/ERROR()` macros. Update pattern to include `%s:%#`. This is the single highest-impact change: every log line becomes self-identifying. **Why essential:** Without source location, none of the other features (timing, context, JSON) can be traced back to their call site.
- [ ] **LOG-09 + LOG-02 (Module tag convention + tagging):** Define hierarchical tag names in a header file (`src/app/log_tags.h`). Assign tags to each source file's log calls. Implement via logger-per-module (each module gets a named `spdlog::logger` clone). Use `%n` in pattern. **Why essential:** Module tags are the namespace for stage timing labels and error context frames. Doing this second prevents rework.
- [ ] **LOG-03 (Stage timing):** Implement `ScopedTimer` RAII class. Instrument pipeline stages (scan, probe, encode, pack). Log duration on scope exit. **Why essential:** This directly answers the third core question ("how long did it take?") and replaces the manual `elapsedMs` anti-pattern already present in the codebase.
- [ ] **LOG-06 + LOG-07 (Per-run files + retention):** Generate timestamped filenames in `setupLogging()`. Clean up old files on startup (keep last 10). **Why essential:** Makes log comparison across runs possible and prevents unbounded disk growth. This is the smallest change with the largest operational impact.

### Add After Validation (v1.x)

Features to add once core logging is production-quality.

- [ ] **LOG-04 (Error context chain):** Implement thread-local context stack + RAII `ScopedContext` guard. On `SPDLOG_ERROR`, auto-append the chain traceback. **Trigger for adding:** After first production debugging session where stage timing alone was insufficient to trace a complex failure. The context chain becomes most valuable when you hit an error whose cause is non-obvious from a single log line.
- [ ] **LOG-05 (Environment snapshot):** Implement component registry for shared state. On error, snapshot slot states, pending counts, FFmpeg process info. **Trigger for adding:** After encountering a nondeterministic parallel encoding failure where the operation chain alone wasn't enough to identify the cause (e.g., race conditions, resource exhaustion).

### Future Consideration (v2+)

Features to defer until the core logging is battle-tested.

- [ ] **LOG-08 (JSON output):** Implement custom `pattern_formatter` for JSON. Gate behind `--log-json` flag. **Why defer:** JSON output is a formatting layer on top of existing structured data. Until source location, module tags, timing, and error context are all producing structured metadata, JSON output is sparse and low-value. Implementing JSON first means reworking it when each feature adds new fields. Build the data first, then add the output format.

## Feature Prioritization Matrix

| Feature | User Value | Implementation Cost | Priority |
|---------|------------|---------------------|----------|
| LOG-01: Source location injection | HIGH | LOW | P1 |
| LOG-09: Module tag naming convention | MEDIUM | LOW | P1 |
| LOG-02: Module tagging implementation | HIGH | MEDIUM | P1 |
| LOG-03: Stage auto-timing | HIGH | LOW | P1 |
| LOG-06: Per-run log files | MEDIUM | LOW | P1 |
| LOG-07: Retention cleanup (last 10) | MEDIUM | LOW | P1 |
| LOG-04: Error operation chain | HIGH | MEDIUM | P2 |
| LOG-05: Error environment snapshot | HIGH | HIGH | P2 |
| LOG-08: JSON structured output | MEDIUM | MEDIUM | P3 |

**Priority key:**
- P1: Must have for launch (v1). These six features collectively answer "where, what, how long" and make the log self-managing.
- P2: Should have, add after validation. Error context features are high-value but require the foundation (tags, timing) to be solid first.
- P3: Nice to have, future consideration. JSON output is valuable for CI/tooling but adds the least to the core debugging experience.

## Competitor Feature Analysis

Analysis of how production CLI tools and media encoding tools handle logging.

| Feature | ffmpeg CLI | HandBrake CLI | x264 CLI | Our Approach |
|---------|------------|---------------|----------|--------------|
| Log levels | `-v quiet/panic/fatal/error/warning/info/verbose/debug/trace` + `-loglevel` | `--verbose` (single flag) | `--quiet`, `--verbose`, `--log-level` (string) | `--verbose` (on/off with spdlog level hierarchy). Simpler is better for a focused tool. |
| Source location | No (bare messages) | No (bare messages) | No (bare messages) | Yes -- `file.cpp:128` via `SPDLOG_*` macros. Unique differentiator for debuggability. |
| Module tagging | Module prefix in brackets `[libx264 @ 0x...]` | Component name prefix | Module prefix in brackets | Hierarchical tags `[video.encode]`, `[pack.zip]`. More structured and `grep`-friendly than free-form prefixes. |
| Per-run log files | Manual via `2> log.txt` by user | `--log-file` flag, overwrites | Manual via shell redirect | Automatic per-run `encro_YYYYMMDD_HHMMSS.log`. No user action needed. |
| Log retention | None (user-managed) | None (user-managed) | None (user-managed) | Automatic last-10 retention. Reduces user maintenance burden. |
| Stage timing | `-benchmark` prints total encode time only | No | No per-stage timing | Per-stage auto-timing with RAII scoped timers. Granular pipeline insight. |
| Error context | Prints error code + sometimes suggestion | Prints error message | Prints error message | Full operation chain traceback + environment snapshot. Answers "what was happening?" not just "what failed?" |
| Structured output | No (text only, parseable but not structured) | No (text only) | No (text only) | Optional `--log-json` for machine-parseable output. |
| Progress reporting | `-progress` writes structured pipe protocol | Built-in progress bars | `--progress` flag | Already has progress bars via `indicators`. Logging layer complements this. |
| Crash handler | None (crash = OS dialog) | None | None | SEH/signal handlers with stacktrace + logger flush. Catches and logs crashes. |

**Key insight:** ffmpeg, HandBrake, and x264 are the direct competitors in the media encoding CLI space. None of them provide source location in logs, automatic per-run files, retention, or structured error context. These are genuine differentiators that make encro's log more useful for debugging than any competitor's. The ffmpeg approach of manual `2> log.txt` and the user being responsible for log management is the industry norm -- encro's automatic approach is strictly better.

## Sources

- spdlog pattern syntax and source location flags: [spdlog Pattern Syntax (DeepWiki)](https://deepwiki.com/gabime/spdlog/3.4.1-pattern-syntax), [spdlog custom flag formatter (CSDN)](https://cpplus.blog.csdn.net/article/details/140334719)
- spdlog JSON logging: [Official Wiki: Setting up JSON logging with spdlog](https://github.com/gabime/spdlog/wiki/Setting-up-JSON-logging-with-spdlog), [structured_spdlog community extension](https://github.com/bobhansen/structured_spdlog)
- spdlog file rotation and retention: [File Rotation (DeepWiki)](https://deepwiki.com/gabime/spdlog/6-file-rotation), [Size-based Rotation](https://deepwiki.com/gabime/spdlog/6.1-size-based-rotation)
- spdlog per-run timestamped filenames: [GitHub Discussion #2465](https://github.com/gabime/spdlog/discussions/2465), [Rotating file sink max files issue #1566](https://github.com/gabime/spdlog/issues/1566)
- Production C++ logging best practices: Folly Logging overview, NanoLog paper, XTR logging library, IBM Alchemy Logging
- RAII scoped timer pattern: Adobe Lagrange `ScopedTimer`, StackOverflow community patterns, NetworKit `LoggingTimer`
- Competitor analysis: ffmpeg CLI `-loglevel` documentation, HandBrake CLI `--verbose` flag, x264 CLI logging flags
- encro project context: `.planning/PROJECT.md`, `src/app/prelude.cpp`, `src/infra/crash_runtime.cpp`, `AGENTS.md`

---
*Feature research for: encro CLI media encoding tool logging system*
*Researched: 2026-05-23*
