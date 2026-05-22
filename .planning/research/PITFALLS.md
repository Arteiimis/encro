# Pitfalls Research

**Domain:** C++ spdlog logging system enhancement (multithreaded CLI application)
**Researched:** 2026-05-23
**Confidence:** HIGH

## Critical Pitfalls

### Pitfall 1: `__FILE__` / `__LINE__` Injection via Function Instead of Macro

**What goes wrong:**
Using a function wrapper (e.g., `log_info(msg, source_location::current())`) for source location injection causes every log statement to report the wrapper function's file and line, not the actual call site.

**Why it happens:**
`__FILE__`, `__LINE__`, and `std::source_location::current()` are evaluated at the point they appear in source. A function wrapper evaluates them inside the wrapper function, not at the caller. This is the single most common mistake in C++ logging enhancement — it renders source location useless.

**How to avoid:**
Use macros that expand at the call site. For this project, create a header (e.g., `log_macros.h`) that defines:

```cpp
// Option A: Inject into message body (safest for async, avoids source_loc lifetime issues)
#define ENCRO_DEBUG(...) \
    do { if (spdlog::default_logger()->should_log(spdlog::level::debug)) \
        spdlog::debug("[{}:{}] {}", SPDLOG_SHORT_FILE, __LINE__, fmt::format(__VA_ARGS__)); \
    } while(0)

// Option B: Use spdlog built-in pattern flags %@/%s/%#/%!
// Requires: spdlog::set_pattern("[%s:%#] [%^%l%$] %v");
// Then use: SPDLOG_DEBUG("message")  // spdlog's own macro, not spdlog::debug()
```

The project already uses `spdlog::debug()` (the function, not the macro). This MUST change to `SPDLOG_DEBUG()` or a custom macro to get correct call-site location. 19 source files need migration.

**Warning signs:**
- Every log line shows the same source location (your wrapper source file)
- `source_location::current()` or `__LINE__` is inside a function, not a macro
- Using `spdlog::debug()` (function) when the pattern contains `%@` or `%s:%#`

**Phase to address:**
LOG-01 (Source Location). Must be architected correctly before any code migration begins.

---

### Pitfall 2: Async `source_loc` Use-After-Free

**What goes wrong:**
When using spdlog's built-in `%@` / `%s` / `%#` pattern flags with async logging, the `source_loc` struct contains raw `const char*` pointers. Under certain conditions the compilation unit's constants can be unmapped before the async worker thread formats the message, causing a crash or garbage output.

**Why it happens:**
spdlog's `source_loc` stores `const char* filename` as a non-owning pointer. In async mode the message is queued and formatted on a worker thread. Normally `__FILE__` expands to a compile-time string literal with static lifetime — safe. But: (1) `dlclose`/`FreeLibrary` unloading shared libraries, (2) custom source location from non-compile-time sources (e.g., script VMs), or (3) passing `std::source_location` across TU boundaries can produce dangling pointers.

**How to avoid:**
For this project (Windows x64, no dynamic library unloading during logging), the risk is LOW but not zero. Mitigation:
- **Inject source location into the message body** (Option A in Pitfall 1) rather than using `%@` / `%s` / `%#` pattern flags. The message string is owned/copied when queued.
- If using pattern flags, ensure `spdlog` version >= 1.12 where source_loc deep-copy was improved.
- Never pass `std::source_location` from external/DLL code into spdlog in async mode.
- Monitor [spdlog#2867](https://github.com/gabime/spdlog/issues/2867) for the owning-string API evolution.

**Warning signs:**
- Intermittent crashes in spdlog worker thread with corrupted source file name strings
- Crashes only reproducible in Release builds (optimization-dependent)
- ASAN/UBSAN reports of use-after-free in `pattern_formatter::format()`

**Phase to address:**
LOG-01 (Source Location). Decision on macro vs pattern-flag approach determines exposure.

---

### Pitfall 3: Thread-Local Context Bleeds into Async Log Messages

**What goes wrong:**
Error context accumulation (LOG-04), scoped timing (LOG-03), or module tagging (LOG-02) stored in thread-local storage (TLS) gets captured at log-call time but formatted on the async worker thread — which has different TLS. The worker thread sees empty or wrong context.

**Why it happens:**
spdlog async: the caller thread queues `log_msg` (which captures the message string), then the worker thread formats and writes it. Thread-local variables on the caller thread are invisible to the worker thread. This is the exact problem documented in [spdlog MDC documentation](https://deepwiki.com/gabime/spdlog/7.4-mdc-and-context): "MDC is not supported in asynchronous logging mode due to its reliance on thread-local storage."

**How to avoid:**
Resolve context at the call site, before queuing. Three approaches in order of preference:

1. **Bake context into the message string at call time** (recommended): The macro that logs also injects the current module tag and any accumulated context. Example:
   ```cpp
   #define ENCRO_DEBUG(mod, ...) \
       spdlog::debug("[{}] {}", (mod), fmt::format(__VA_ARGS__))
   ```
   The context is resolved and serialized while on the calling thread.

2. **Use thread-local accumulators flushed at log points**: Store context in TLS, but when `ENCRO_DEBUG()` is called, read TLS and inject it into the format string immediately. Never rely on the worker thread reading TLS.

3. **Custom formatter that copies context at queue time**: Extend `log_msg` with an owned context payload filled at the call site. Overkill for this project.

**Warning signs:**
- Module tags are empty or wrong in async log output
- Error context shows state from the wrong operation
- Timing data shows 0ms or garbage values
- Everything works when switching to `basic_logger_mt` (synchronous) but breaks with `async_logger`

**Phase to address:**
LOG-02, LOG-03, LOG-04. Every feature that carries per-operation context must resolve it at the call site.

---

### Pitfall 4: Clock Drift Between `std::chrono::steady_clock` Timers and spdlog Wall-Clock Timestamps

**What goes wrong:**
Scoped timers (LOG-03) using `std::chrono::steady_clock::now()` record durations, but spdlog timestamps use `std::chrono::system_clock`. The two clocks have different epochs and different susceptibility to NTP adjustments. When post-processing logs, correlating timer-reported durations with wall-clock timestamps produces confusing results (e.g., an operation "ends" before it "began" per wall clock).

**Why it happens:**
`steady_clock` is monotonic — it never goes backward. `system_clock` can jump due to NTP. Using both in the same log line without awareness creates an apparent time paradox: wall-clock timestamps may drift relative to measured durations. Also, `steady_clock` has no defined epoch, so its raw values are meaningless outside program scope.

**How to avoid:**
- **Always pair timer entries with spdlog timestamps**: A "started" log line at the beginning (system_clock timestamp) and an "elapsed: Xms" log line at the end (steady_clock duration). Never mix clock domains in the same semantic timestamp.
- Use `steady_clock` exclusively for duration measurement.
- If wall-clock correlation matters, record `system_clock::now()` at process start and compute offsets relative to it for approximate mapping — but document the inherent imprecision.
- For JSON output (LOG-08), include both `"wall_ts"` (system_clock ISO-8601) and `"elapsed_ms"` (steady_clock delta) as separate fields, never a single "adjusted" timestamp.

**Warning signs:**
- Log analysis shows operations with negative duration (system_clock jumped backward)
- Phase start/end timestamps in JSON don't align with message timestamps
- Impossible to determine actual wall-clock time of intermediate events

**Phase to address:**
LOG-03 (Scoped Timing) + LOG-08 (JSON Output).

---

### Pitfall 5: Single-Thread Pool Bottleneck with High-Logging Pipelines

**What goes wrong:**
The current system uses a 1-thread pool with blocking overflow policy and 8192 queue slots. When adding source location, module tags, scoped timers, and error context to every log line, the per-message formatting cost increases. Under high-throughput scenarios (encoding pipeline with 10 concurrent slots logging progress per frame), the single async worker thread becomes a bottleneck: producer threads block when the queue fills, stalling the encoding pipeline.

**Why it happens:**
The `mpmc_blocking_queue` uses a single mutex for all enqueue/dequeue operations. With the `block` overflow policy, when 8192 messages queue up, every subsequent logging call blocks the caller until the worker thread drains items. Additional formatting (source_loc, module tags, timing) increases the worker thread's per-item processing time. At high volumes, this creates a positive feedback loop: slower formatting -> slower drain -> more blocking -> more pipeline stall.

Benchmarks from the spdlog community show async logging can be 50-60% slower than synchronous `_mt` logging under high contention due to the queue mutex bottleneck.

**How to avoid:**
1. **Profile first**: Add the features, then measure with a worst-case batch (many small files, verbose logging). Don't pre-optimize.
2. **Queue size**: Increase from 8192 to 32768 before increasing thread count. Larger queue buys headroom without ordering issues.
3. **Thread count**: Keep at 1 unless ordering genuinely doesn't matter. Multiple threads break message ordering.
4. **Rate limiting**: For high-frequency messages (encoding progress per-frame), consider a rate-limiting macro that skips log calls if called too recently:
   ```cpp
   #define ENCRO_DEBUG_RATELIMITED(interval_ms, ...) \
       do { static auto last = std::chrono::steady_clock::time_point{}; \
            auto now = std::chrono::steady_clock::now(); \
            if (now - last > std::chrono::milliseconds(interval_ms)) { \
                last = now; ENCRO_DEBUG(__VA_ARGS__); \
            } \
       } while(0)
   ```
5. **Level gating**: Use `logger->should_log()` check in macros to avoid formatting work for disabled levels. This is why macros matter — function calls always evaluate arguments.

**Warning signs:**
- Encoding pipeline throughput drops significantly when `--verbose` is enabled
- Worker thread CPU usage at 100% while producer threads idle
- Queue full messages (or blocking behavior) visible in profiling
- Log output lags significantly behind actual execution progress

**Phase to address:**
All phases. Performance regression testing must be part of every phase's acceptance criteria.

---

### Pitfall 6: Per-Run Log File Rotation Colliding with Async Drain

**What goes wrong:**
When implementing per-run log files (LOG-06) and cleanup of old files (LOG-07), the async queue may still contain unflushed messages when the program starts cleaning up log files from previous runs. More critically, if the log sink is recreated mid-run (e.g., for log rotation), queued messages from the old sink are lost or written to a closed file handle.

**Why it happens:**
spdlog's `async_logger` holds a reference to sinks. If sinks are swapped while messages are queued, those messages reference the old sink. With per-run file naming, the sink is created once at startup — this part is safe. But if any future feature adds mid-run rotation or sink-switching, queued messages are at risk.

**How to avoid:**
1. **Create sinks before logger**: The current code does this correctly (build sinks vector, then construct logger). Maintain this ordering.
2. **Call `logger->flush()` before shutdown**: The current code only `flush()`es on error. Add an explicit `spdlog::shutdown()` call in `main()` return path or atexit to drain the queue before program exit.
3. **For per-run files**: Create the file sink with the timestamped name at startup, pass it to the async_logger, and never change it during the run. This is the simplest and safest approach.
4. **For cleanup (LOG-07)**: Clean up old files at startup, BEFORE creating the new file sink. This avoids any race between cleanup and the logger actively writing.
5. **Atomic rename for rotation**: If per-run files are ever replaced with a rotating pattern, use `rename()` (atomic on same filesystem) rather than copy+delete for the current log file.

**Warning signs:**
- Truncated log files (last few messages missing)
- "File not found" errors in log cleanup code when logger is still writing
- Messages appearing in the wrong file after a rotation event
- ASAN reports of use-after-free on sink pointers during shutdown

**Phase to address:**
LOG-06, LOG-07 (Per-run files and cleanup).

---

### Pitfall 7: JSON Message Escaping Breaks Structured Parsing

**What goes wrong:**
When enabling `--log-json` output (LOG-08), raw message strings containing double quotes, backslashes, newlines, or control characters produce invalid JSON that downstream tools cannot parse.

**Why it happens:**
spdlog's `%v` pattern flag inserts the raw message text directly. If the message contains `"`, `\`, `\n`, or unescaped control characters, the resulting JSON is syntactically invalid. For example, an FFmpeg error message containing `Error: "file not found"` becomes `{"msg": "Error: "file not found""}` — broken JSON. This is explicitly documented in spdlog's JSON logging guide.

**How to avoid:**
1. **Custom JSON formatter**: Create a `json_formatter` class (inheriting from `spdlog::formatter`) that escapes all string fields using a proper JSON escaping function. Do NOT rely on the pattern string to produce valid JSON.
2. **Separate sink for JSON**: Create a dedicated file sink with the JSON formatter, and a separate stdout sink with the human-readable formatter. Use a single `async_logger` with both sinks. The `--log-json` flag controls whether to add the JSON sink.
3. **Structured fields**: When adding error context (LOG-04) and timing (LOG-03), emit them as separate JSON fields rather than embedding them in the message string. This requires extending `log_msg` or injecting pre-formatted fragments.
4. **Escape early**: If injecting into the message body, escape before queuing. Never assume downstream tools will tolerate malformed JSON.

**Warning signs:**
- `jq`, `python -m json.tool`, or any JSON parser fails on log output
- Log lines containing FFmpeg paths with backslashes (Windows) break JSON
- Newlines in error messages produce multi-line JSON records
- Test suite for JSON output only tests with ASCII alphanumeric messages

**Phase to address:**
LOG-08 (JSON output).

---

### Pitfall 8: Module Tag Proliferation Creates Naming Chaos

**What goes wrong:**
Without a defined, enforced naming convention for module tags (LOG-02, LOG-09), each developer invents their own scheme: `[video.encode]`, `[video_encoder]`, `[encoding]`, `[vid:encode]` all refer to the same component. Log filtering/greping becomes unreliable.

**Why it happens:**
19 source files, each with their own logging calls. Without a single header defining canonical tag names as constants, developers copy-paste from nearby code and mutate the tag. The requirement LOG-09 ("define and enforce module tag naming convention") exists specifically to prevent this, but enforcement requires more than a documentation page.

**How to avoid:**
1. **Single header of tag constants**:
   ```cpp
   // log_tags.h
   namespace logtags {
       inline constexpr auto VIDEO_ENCODE  = "video.encode";
       inline constexpr auto VIDEO_PROBE   = "video.probe";
       inline constexpr auto VIDEO_SCAN    = "video.scan";
       inline constexpr auto PACK_ZIP      = "pack.zip";
       inline constexpr auto PACK_SERVICE  = "pack.service";
       inline constexpr auto PICT_COMPRESS = "picture.compress";
       inline constexpr auto CMD_PARSE     = "cmd.parse";
       inline constexpr auto CRASH         = "crash";
   }
   ```
2. **Hierarchical (dot-separated) convention**: `component.subcomponent`. Enables prefix-based filtering: `video.*` matches all video module logs.
3. **Compile-time enforcement via macro**: The logging macro requires a tag from the constants, not a raw string:
   ```cpp
   #define ENCRO_INFO(tag, ...) \
       spdlog::info("[{}] {}", tag, fmt::format(__VA_ARGS__))
   // Usage: ENCRO_INFO(logtags::VIDEO_ENCODE, "start: {}", path);
   ```
   This doesn't prevent using wrong constants, but makes correct usage the path of least resistance.
4. **Test for unknown tags**: A test that greps all logging calls in source and verifies every tag string matches a known constant. Run in CI.

**Warning signs:**
- `git grep "spdlog::" | grep "\["` shows inconsistent tag formats
- Same component tagged differently in different files
- Tags have typos that survive code review
- No single location documents all valid tags

**Phase to address:**
LOG-02 + LOG-09 (Module tagging and naming convention). Must be architected before migration of individual source files begins.

---

### Pitfall 9: Error Context Accumulation Growing Unbounded

**What goes wrong:**
Error context accumulation (LOG-04) — collecting breadcrumb trail across operations — grows memory without bound in long-running batch processing. If context is accumulated for every file in a 10,000-file batch without pruning, memory usage grows linearly with batch size.

**Why it happens:**
Natural design instinct is to push context onto a stack at each pipeline stage and pop on completion. But if the context store is global/thread-local and never pruned (e.g., failure to pop on early return, exception, or cancel), it accumulates forever. Additionally, spdlog async mode means the log message must capture context at call time (Pitfall 3), so the context itself must be small enough to copy into each message.

**How to avoid:**
1. **Operation-scoped, not global**: Context belongs to a specific operation (encoding one file), not to the entire process. Use RAII context guards that push on construction and pop on destruction. The `video_batch_execution.cpp` pattern of per-file `EncodingState` is already the right model.
2. **Capture, don't reference**: When logging with context, serialize the relevant context snapshot into the message. Don't store a pointer to a growing context list.
3. **Size limits**: Cap the breadcrumb trail at a reasonable depth (e.g., last 20 events). Beyond that, log a truncation marker.
4. **Environment snapshot (LOG-05) is point-in-time**: Capture it only when an error occurs, not continuously. Use the existing `jobstate::Store` to query concurrent slot states on demand.
5. **Circular buffer for breadcrumbs**: A fixed-size ring buffer per operation. If an operation has more than N state transitions, older entries are silently dropped.

**Warning signs:**
- Memory usage grows linearly with batch size when `--verbose` is enabled
- Context objects reference each other, preventing cleanup
- Cancelled or failed operations leak context because destructors don't run
- Profiling shows `std::vector` or `std::string` growth proportional to file count

**Phase to address:**
LOG-04 (Error context). Architecture design must include memory bounds.

---

### Pitfall 10: Crash Handler Competing with Per-Run File After Shutdown

**What goes wrong:**
The crash handler (`crash_runtime.cpp`) calls `logger->critical()` + `logger->flush()` during signal/exception handling. If the per-run file sink (LOG-06) has already been closed or the async queue drained during shutdown, the crash handler writes to a dead logger. The crash information is silently lost.

**Why it happens:**
`spdlog::shutdown()` drops the default logger and stops the thread pool. If a crash occurs during the shutdown sequence (or in a destructor after `main()` returns), the crash handler's `tryWriteToLogger()` sees `default_logger_raw() == nullptr` and falls back to stderr. While stderr fallback is correct, the per-run log file loses crash context that would be invaluable for debugging. Additionally, crash handler calls `logger->flush()` which may reference a destroyed thread pool.

**How to avoid:**
1. **Call `spdlog::shutdown()` ONLY as the last thing in `main()`**, after all other cleanup and after crash handler unregistration (if you unregister it). Better yet, don't call shutdown at all — let static destruction handle it, as the crash handler is designed to survive beyond `main()`.
2. **Keep crash handler's `tryWriteToLogger` with stderr fallback** — the current pattern is correct.
3. **For per-run files**: The crash handler's `writeToStderr` fallback should also attempt to append to the log file directly via a known path, bypassing spdlog. This is a defensive measure:
   ```cpp
   void writeCrashToLogFile(std::string const& msg) {
       // Direct file append, bypassing spdlog entirely
       auto ofs = std::ofstream(gLogFilePath, std::ios::app);
       if (ofs) ofs << msg << std::endl;
   }
   ```
4. **DO NOT register the crash handler as a spdlog flush callback** that might be cleaned up before the crash happens.

**Warning signs:**
- Crash reports in CI show only stderr output, per-run log file has no crash entry
- Segfault during `spdlog::shutdown()` after a crash
- Race condition: crash handler fires while spdlog thread pool is being destroyed

**Phase to address:**
LOG-06 (Per-run files) + existing crash handler integration. Must coordinate with shutdown sequencing.

---

## Technical Debt Patterns

Shortcuts that seem reasonable but create long-term problems.

| Shortcut | Immediate Benefit | Long-term Cost | When Acceptable |
|----------|-------------------|----------------|-----------------|
| Using `spdlog::debug()` (function) instead of `SPDLOG_DEBUG()` (macro) with source location pattern flags | No code changes needed in 19 files | Source location shows prelude.h:117 for every log line — completely useless | Never. This defeats LOG-01 entirely. |
| Hardcoding module tags as string literals in each `spdlog::info()` call | Quick to implement, no new header | Inconsistent naming, impossible to refactor tags, grep-only enforcement | Prototype phase only. Not for production. |
| Single global context accumulator for breadcrumb trail | Simple API, no per-operation wiring | Context from different operations interleaves, memory growth unbounded, no cleanup on cancel | Never. Must be operation-scoped. |
| JSON output via pattern string with no escaping | One-line pattern change | Invalid JSON on first FFmpeg path containing backslash or error message containing double quote | Never for JSON. Pattern strings cannot properly escape `%v`. |
| Adding a second thread to the async pool to handle increased message load | Appears to fix throughput | Message ordering becomes nondeterministic, making log correlation impossible for debugging | Only if ordering is explicitly documented as non-guaranteed. |
| Log cleanup (LOG-07) running in a background thread during execution | Non-blocking | Race condition: cleaner deletes a file the logger is about to rotate to; crash handler can't find log path | Never. Cleanup at startup only. |

## Integration Gotchas

Common mistakes when connecting to existing systems.

| Integration | Common Mistake | Correct Approach |
|-------------|----------------|------------------|
| `BS::thread_pool` encoding slots | Assuming `spdlog::info()` inside a `detach_task` lambda captures slot context from the outer scope. All slots share the same logger. | Pre-format the slot label into the message string. The current manual `[slot:X task:Y/Z]` prefix pattern is correct — enhance with macro but keep the approach. |
| `crash_runtime.cpp` + async logger | Calling `logger->critical()` and assuming it's flushed before the crash terminates the process. | The current `logger->flush()` after `critical()` is correct. Do NOT change to rely on the async queue alone. The flush must stay synchronous-on-error. |
| `stop_signal.h` / Ctrl+C | Logging `spdlog::warn("Canceled by user")` inside a signal handler. spdlog uses mutexes internally — calling from signal handler can deadlock. | Do NOT log from signal handlers. Set a flag; check the flag in the main loop and log from there. The current `stopsignal::isStopRequested()` pattern checked in `runEncodingTask()` is correct. |
| FFmpeg external process stderr | Parsing FFmpeg stderr output for progress but not logging it to the enhanced logger. | FFmpeg stderr should be routed through a dedicated log call with the `video.encode` module tag and per-file context. The existing `utils.cpp` command execution already logs commands — extend this with structured context. |
| `stdout_color_sink_mt` + `--verbose-echo` | JSON output with color codes in the stdout sink. Colors are ANSI escape sequences — they corrupt JSON if the same pattern is used. | Separate sinks: file sink gets JSON formatter, stdout sink keeps human-readable formatter. `--log-json` controls whether the JSON sink is added, NOT whether the human-readable sink is modified. |
| `spdlog::set_pattern()` global state | Calling `set_pattern()` anywhere other than `setupLogging()` in `prelude.cpp`. Pattern is global — a second call overwrites the first. | Pattern must be set exactly once, in `setupLogging()`. If different sinks need different patterns, set pattern on individual sinks, not globally. |

## Performance Traps

Patterns that work at small scale but fail as usage grows.

| Trap | Symptoms | Prevention | When It Breaks |
|------|----------|------------|----------------|
| Every per-frame encoding progress update gets a log entry | Log file grows to gigabytes; async queue fills; encoding stalls | Rate-limit encoding progress logs; use `debug` level for per-frame, `info` only for start/complete | ~100 files with verbose + 10 concurrent slots each producing ~1000 progress updates = 1M log lines |
| Scoped timer destructor performing blocking I/O | Encoding slot blocked waiting for timer log to queue; cascading stall across all slots | Timer logs use `debug` level; format duration into message at call site (not in destructor I/O). The RAII guard collects `steady_clock::time_point`; destructor only formats and calls the macro — formatting is the expensive part, move it to the macro level-check. | Any batch with > 50 files where timer granularity is per-function |
| JSON formatter doing O(n) field lookup per message | JSON output is 3-10x slower than plain text; async queue never drains | Use indexed field access, not map lookup per field. Pre-compute field name hashes. Consider JSON output as `debug`-only in the first iteration. | ~500+ messages/second |
| `std::source_location::current()` (C++20) on every log call | Slower than macro-based approach; still requires a macro to capture call site | Use `__FILE__`/`__LINE__` in macros. `std::source_location` adds function name at runtime via `__builtin_FUNCTION()` — a small but measurable cost per call. For this project's scale, the difference is negligible; prefer whichever integrates cleaner with spdlog. | ~10K+ messages/second (not relevant for this project) |

## Security Mistakes

Domain-specific security issues beyond general security.

| Mistake | Risk | Prevention |
|---------|------|------------|
| Logging full file paths from user input without sanitization | Log injection — a filename containing newlines can forge log entries. Path traversal info leak. | Use `displaytext::pathToUtf8String()` (already used in `makeSlotLabel()`). Do not log raw `fs::path::string()` without going through a display utility. |
| Logging FFmpeg command lines with user-controlled arguments | Passwords or tokens in video URLs appear in plaintext logs. Command injection in log viewers. | Sanitize FFmpeg command lines before logging: strip query parameters from URLs, truncate long argument values. Currently `utils.cpp:40` logs the full command — this needs review. |
| Per-run log files in world-readable directories | Other users on the system can read processed file paths, revealing directory structures and file names | `%LOCALAPPDATA%` is user-scoped on Windows (correct). For POSIX, use `~/.local/state/encro/logs/` with `0700` permissions. |
| JSON log output containing stack traces with absolute paths | Discloses build/user directory structure to anyone consuming the JSON log | Truncate file paths to project-relative where possible. Use `-fmacro-prefix-map` build flag to strip build directory from `__FILE__`. |

## UX Pitfalls

Common user experience mistakes in this domain.

| Pitfall | User Impact | Better Approach |
|---------|-------------|-----------------|
| `--log-json` flag silently outputting both JSON and human-readable text to the same file | User pipes log to `jq` and gets parse errors on the human-readable lines | Dedicated JSON file sink (e.g., `encro_20260523_143052.json`) when `--log-json` is active. Human-readable sink unchanged. |
| Per-run log file naming uses only date, not time | Running twice in the same day overwrites the first run's log | Timestamp to second precision: `encro_20260523_143052.log`. The specification uses this format — verify implementation. |
| Log cleanup deletes the current run's log file | User loses the log they're currently generating | Cleanup runs before the new file is created. Do not include the just-created timestamped name in the cleanup glob. |
| Module tag format changes break user's grep aliases | User has `alias encro-errors='grep "\[video" encro.verbose.log'` that stops working | Document the tag hierarchy. Provide a `--log-tags` help flag that prints all valid tags. |

## "Looks Done But Isn't" Checklist

Things that appear complete but are missing critical pieces.

- [ ] **Source location (LOG-01):** Often missing: logging calls that still use `spdlog::debug()` (function) instead of the macro. Verify: `git grep "spdlog::debug\|spdlog::info\|spdlog::warn\|spdlog::error" src/` returns zero results (all migrated to macros).
- [ ] **Module tags (LOG-02):** Often missing: tags not appearing in crash handler output. Verify: crash handler log entries contain module tags.
- [ ] **Scoped timing (LOG-03):** Often missing: early returns and exceptions not recording duration. Verify: RAII guard with `noexcept` destructor — timer always records even on throw.
- [ ] **Error context (LOG-04):** Often missing: context cleanup on cancel/stop. Verify: Ctrl+C during batch encoding produces complete context trail, not truncated/empty.
- [ ] **Environment snapshot (LOG-05):** Often missing: snapshot captured too late (after state has changed). Verify: snapshot is the first thing captured when an error is detected, before any cleanup.
- [ ] **Per-run files (LOG-06):** Often missing: crash handler path not updated to the per-run file. Verify: crash appears in the timestamped log, not a fallback file.
- [ ] **Log cleanup (LOG-07):** Often missing: cleanup glob matches files from other applications. Verify: cleanup pattern is specific to `encro_*.log` and only operates in the encro log directory.
- [ ] **JSON output (LOG-08):** Often missing: JSON valid for non-ASCII messages (FFmpeg Chinese/Japanese error messages). Verify: test with Unicode FFmpeg output.
- [ ] **Tag naming (LOG-09):** Often missing: naming convention document exists but tags in code don't match. Verify: automated test that parses all log calls and validates tag format.

## Recovery Strategies

When pitfalls occur despite prevention, how to recover.

| Pitfall | Recovery Cost | Recovery Steps |
|---------|---------------|----------------|
| Source location via function instead of macro (Pitfall 1) | MEDIUM | Regenerate `SPDLOG_DEBUG` etc. migration across all 19 source files. grep-and-replace is mechanical but tedious. Write a `sed` script or clang-tidy fixit. |
| TLS with async (Pitfall 3) | HIGH | Redesign context injection to resolve at call site. May require restructuring context accumulator APIs. Touches every file with context-aware logging. |
| JSON escaping via pattern string (Pitfall 7) | MEDIUM | Replace pattern-string JSON with a custom `json_formatter` class. Sink configuration changes only; message format string changes in macros. |
| Async queue bottleneck (Pitfall 5) | LOW | Increase queue size. If that fails, add rate limiting or switch to `async_overflow_policy::overrun_oldest` for debug-level messages. No code structure changes. |
| Module tag chaos (Pitfall 8) | MEDIUM | Refactor all tag strings to constants. `sed`-able but requires careful review to consolidate synonyms. |
| Error context memory leak (Pitfall 9) | HIGH | Restructure context from global to operation-scoped with RAII guards. Touches per-operation code paths. May require thread-local context redesign. |

## Pitfall-to-Phase Mapping

How roadmap phases should address these pitfalls.

| Pitfall | Prevention Phase | Verification |
|---------|------------------|--------------|
| Source location via function (Pitfall 1) | LOG-01 | All `spdlog::debug()` calls replaced with macros; source location pattern shows correct call site |
| Async source_loc UAF (Pitfall 2) | LOG-01 | Decision documented: inject into message body vs. pattern flags; ASAN clean on batch run |
| TLS with async (Pitfall 3) | LOG-02, LOG-03, LOG-04 | Context fields populated in async log output; module tags correct per-message |
| Clock drift (Pitfall 4) | LOG-03, LOG-08 | JSON output has separate wall_ts and elapsed_ms fields; no negative durations in analysis |
| Async queue bottleneck (Pitfall 5) | All phases | Performance benchmark unchanged from pre-enhancement baseline |
| File rotation + async drain (Pitfall 6) | LOG-06, LOG-07 | Last N lines of log not truncated; cleanup runs before file creation |
| JSON message escaping (Pitfall 7) | LOG-08 | `jq` parses full log output; Unicode FFmpeg messages produce valid JSON |
| Module tag chaos (Pitfall 8) | LOG-02, LOG-09 | CI test validates all tags against known constants; no raw string tags in source |
| Error context memory growth (Pitfall 9) | LOG-04 | Memory usage constant per file processed; no unbounded growth in batch |
| Crash handler + per-run files (Pitfall 10) | LOG-06 | Crash appears in per-run log; crash handler survives spdlog shutdown |

## Sources

- [spdlog Custom Formatting / Pattern Flags](https://spdlog.docsforge.com/master/3.custom-formatting/#pattern-flags) — official pattern flag reference
- [spdlog Asynchronous Logging](https://github.com/gabime/spdlog/wiki/Asynchronous-logging) — official async logging documentation
- [spdlog Issue #235: source file name and line number logging](https://github.com/gabime/spdlog/issues/235) — macro requirement for source location
- [spdlog Issue #2867: source_loc support for owning strings](https://github.com/gabime/spdlog/issues/2867) — async use-after-free concern
- [spdlog Issue #3190: async loggers output not in order](https://github.com/gabime/spdlog/issues/3190) — multi-thread ordering issue
- [spdlog Issue #2298: static initialization safety](https://github.com/gabime/spdlog/issues/2298) — factory methods during static init
- [spdlog Issue #3019: crash when using async_logger with threadpool](https://github.com/gabime/spdlog/issues/3019) — destruction order crash
- [spdlog Issue #3246: daily rotation and confusing target file names](https://github.com/gabime/spdlog/issues/3246) — rotation naming gotchas
- [spdlog Discussion #2151: Multiple loggers using the same file](https://github.com/gabime/spdlog/discussions/2151) — sink sharing patterns
- [spdlog MDC and Context (DeepWiki)](https://deepwiki.com/gabime/spdlog/7.4-mdc-and-context) — TLS incompatibility with async
- [spdlog Custom Format Flags (DeepWiki)](https://deepwiki.com/gabime/spdlog/3.4.2-custom-format-flags) — custom flag formatter and clone() requirement
- [Stack Overflow: spdlog async vs sync performance benchmarks](https://stackoverflow.com/questions/60049438/relatively-low-performance-of-spdlog-when-using-asynchronous-loggers-with-benchm) — queue mutex bottleneck
- [spdlog v1.x README / Wiki (Context7)](https://context7.com/gabime/spdlog/llms.txt) — API reference via Context7
- Project source: `src/app/prelude.cpp` — current logging setup (setupLogging)
- Project source: `src/infra/crash_runtime.cpp` — crash handler integration
- Project source: `src/video/video_batch_execution.cpp` — existing timing and per-slot logging patterns

---
*Pitfalls research for: encro C++ CLI media encoder logging system enhancement*
*Researched: 2026-05-23*
