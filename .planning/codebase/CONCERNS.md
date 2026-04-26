---
focus: concerns
last_mapped_commit: 919b0cea076d2821618c3febf54f72285880cd4c
mapped_at: 2026-04-26
---

# Codebase Concerns

**Analysis Date:** 2026-04-26

## Tech Debt

### Command Injection Risk via File Paths in ffmpeg Command Strings

**Issue:** Both `EncodeConfig::buildCMD()` (`src/video/encode_config.h:81-109`) and `ImageCompressConfig::buildCMD()` (`src/picture/picture_compress.cpp:27-34`) construct ffmpeg command strings by wrapping file paths in double quotes. If a file path itself contains a double-quote character, the quoting breaks and allows arbitrary command injection.

**Files:**
- `src/video/encode_config.h:81-109`
- `src/picture/picture_compress.cpp:27-34`

**Impact:** A maliciously crafted file path containing `"` characters could escape the quoting and inject arbitrary commands into the ffmpeg invocation. Any user or automated process that controls file names on disk can potentially escalate to command execution.

**Fix approach:** Switch to argument-list-based subprocess invocation (pass each arg as a separate string to `boost::process::v1` rather than building a monolithic command string). Alternatively, validate file paths to reject characters with shell significance and escape file paths properly.

### ffmpeg Executable Path Not Quoted in Command Strings

**Issue:** In both `EncodeConfig::buildCMD()` (line 82: `cmd = std::string{ffmpegPath.value().string()}`) and `ImageCompressConfig::buildCMD()` (line 28: same pattern), the ffmpeg executable path is inserted into the command string without double-quote wrapping. File paths are quoted but the executable itself is not.

**Files:**
- `src/video/encode_config.h:82`
- `src/picture/picture_compress.cpp:28`

**Impact:** If ffmpeg is installed at a path containing spaces (common on Windows, e.g., `Program Files`), the subprocess invocation will fail because the path will be treated as multiple arguments.

**Fix approach:** Quote the ffmpeg path with `std::format("\"{}\"", ffmpegPath.value().string())` or switch to argument-list-based subprocess invocation.

### Bare Executable Name Resolution on System PATH

**Issue:** When ffmpeg is found on the system PATH (no custom install directory), `findFFmpeg()` returns `fs::path{"ffmpeg"}` -- a bare executable name with no directory prefix (`src/utils/utils.cpp:340`). `findFFprobe()` does the same (`src/utils/utils.cpp:319`). Subprocess execution then relies on PATH-based resolution at runtime.

**Files:**
- `src/utils/utils.cpp:319, 340`

**Impact:** PATH hijacking risk -- a malicious `ffmpeg.exe` or `ffprobe.exe` placed earlier in PATH would be executed instead of the intended tool. This is especially relevant on shared systems or when running from untrusted directories.

**Fix approach:** Resolve the full absolute path of ffmpeg/ffprobe using `boost::process::search_path()` and store the resolved absolute path in `ToolchainPaths`.

### Inconsistent Error Handling: throw vs eh::Result

**Issue:** The codebase uses `eh::Result<T>` (a `std::expected<T, std::string>` wrapper) consistently for error propagation. However, `EncodeConfig::buildOutputFileName()` (line 49), `buildOutputPath()` (line 71), and `buildCMD()` (line 86) all throw `std::runtime_error` when `inputPath` is missing, bypassing the project convention.

**Files:**
- `src/video/encode_config.h:49, 71, 86`

**Impact:** Callers that handle only `eh::Result` errors will not catch these exceptions, potentially crashing. If an `EncodeConfig` is partially initialized and reaches these methods, the throw propagates without structured error context.

**Fix approach:** Return `eh::makeError(...)` instead of throwing. Make these methods return `eh::Result<std::string>` and `eh::Result<fs::path>` respectively.

### Pre-Commit Hook Hardcoded Absolute Path

**Issue:** The pre-commit hook (`.githooks/pre-commit:4`) contains a hardcoded absolute path: `style_file='D:/clangformat/.clang-format'`. This path only exists on the original developer's machine.

**File:** `.githooks/pre-commit:4`

**Impact:** The pre-commit hook fails for any other developer, blocking commits until they manually edit or bypass the hook.

**Fix approach:** Use a project-relative path (e.g., `.clang-format` in the repo root) or detect via an environment variable with a fallback default.

### Large Files with Mixed Responsibilities

**Issue:** Three files exceed 500 lines and were identified as growth risks in `plans/CODE_REUSE_OPTIMIZATION_PLAN.md` Phase 6:
- `src/pack/packer.cpp` (747 lines): Entry preparation, zip writing, directory scanning, group planning, orchestration.
- `src/video/video_batch_execution.cpp` (753 lines): Monitor thread, progress tracking, slot management, state finalization, encoding task creation.
- `src/core/job_state.cpp` (654 lines): Task records, serialization, config snapshots, state file management, utility functions.

**Files:**
- `src/pack/packer.cpp` (747 lines)
- `src/video/video_batch_execution.cpp` (753 lines)
- `src/core/job_state.cpp` (654 lines)

**Impact:** High cognitive load. Changes to one concern risk touching another. New contributors tend to add logic to existing large files rather than creating focused modules.

**Fix approach:** Monitor line counts. When a file accumulates a new responsibility (not just more of the same concern), extract it. Apply the Phase 6 guard rail: "The largest files stop acting as catch-all locations for new logic."

### Potential Data Loss Window in Job State Persistence

**Issue:** `src/core/job_state.cpp:23` defines `kFlushIntervalMs = 2000` -- the job state file is flushed to disk at most every 2 seconds. On crash or forced termination, up to 2 seconds of task progress and stage transitions can be lost.

**Files:** `src/core/job_state.cpp:23`

**Impact:** After a crash, the resume feature may miss completed tasks or repeat work that was finished but not flushed.

**Fix approach:** Reduce the flush interval, or add opportunistic flushing on critical state transitions (task completion, stage change).

### C++26 Language Standard Usage

**Issue:** `xmake.lua:6` specifies `set_languages("c++26")`. The C++26 standard has not yet been finalized as of April 2026. The codebase uses C++20 features (`std::format`, structured bindings) that work on C++20.

**Files:** `xmake.lua:6`

**Impact:** Potential compiler bugs with draft-standard features. Limits portability to compilers with C++26 front-end support (primarily Clang 19+).

**Fix approach:** Consider downgrading to `c++23` or `c++20` which provide all features the codebase currently uses.

### UTF-8 Path Conversion May Be Lossy

**Issue:** `displaytext::pathToUtf8String()` (`src/core/display_text.h:78-84`) converts `fs::path::u8string()` (returning `std::u8string` of `char8_t`) to `std::string` by casting each `char8_t` to `char`. On platforms where `char` is signed, this can produce negative byte values for non-ASCII characters.

**File:** `src/core/display_text.h:78-84`

**Impact:** File names with non-ASCII characters may display incorrectly in progress bars and status messages.

**Fix approach:** Use `reinterpret_cast<const char*>(utf8.data())` with `utf8.size()` or use `path.string()` directly where the platform encoding is UTF-8.

### Perf Plan Phase 3-5,7 Not Yet Implemented

**Issue:** The `plans/VIDEO_ENCODE_PERF_OPTIMIZATION_PLAN.md` documents 7 optimization phases. Phases 0, 1, 2, 6 are marked complete with `[x]`. Phases 3 (monitor poll interval), 4 (merge critical sections), 5 (WebP starting quality heuristic), and 7 (immer::map to unordered_map) have no completion markers -- suggesting they were planned but either not executed or not documented as complete.

**Areas:**
- Phase 3: Monitor polling still at 20ms (`src/video/video_batch_execution.cpp:339`)
- Phase 5: WebP quality starts at 80 regardless of input size (`src/video/video_encode_runner.cpp:201`)
- Phase 7: `runEncodingWithoutProgress()` still uses `immer::map` in sequential loop (`src/video/video_batch_execution.cpp`)

**Impact:** These are known, documented performance opportunities that remain unaddressed in the current codebase.

## Known Bugs

### E2e Test Build Failure (Pre-Existing)

**Symptoms:** `xmake build e2e_tests` fails with a missing include path. Documented in `plans/VIDEO_ENCODE_PERF_OPTIMIZATION_PLAN.md:78` as "pre-existing build error (missing `infra/stop_signal.h` in e2e `test_utils.h` include path)."

**Files:**
- `tests/e2e/e2e_test_utils.h`
- `tests/e2e/e2e_test_utils.cpp`

**Trigger:** Running `xmake build e2e_tests`.

**Workaround:** None -- the e2e tests cannot be built.

**Fix approach:** Add `src` to the include directories for the `e2e_tests` target in `xmake.lua:83` (currently only `tests` is included; the `tests` target at line 69 correctly includes `src`).

### Directory Iterators Without Error Codes in Packer

**Issue:** `buildDirectoryPackPlan()` in `src/pack/packer.cpp:682-691` uses `fs::recursive_directory_iterator` without an `error_code` parameter. Permission-denied directories will cause an exception that propagates uncaught.

**File:** `src/pack/packer.cpp:682-691`

**Trigger:** Scanning a directory tree containing inaccessible subdirectories in pack-only mode.

**Workaround:** Ensure all files in the scanned directory tree are accessible.

**Fix approach:** Use `fs::directory_options::skip_permission_denied` and pass a `std::error_code` to the iterator constructor, matching the pattern in `media_scanner.cpp:34`.

### `inputSize()` Uses `file_size` Without Checking Error Code

**Issue:** `job_state.cpp:50` calls `fs::file_size(path, ec)` (the non-throwing overload) but does not check `ec` before using the return value. On failure, the function returns `static_cast<uintmax_t>(-1)` which is used as a valid size.

**File:** `src/core/job_state.cpp:50`

**Impact:** Corrupted or inaccessible files could yield `UINTMAX_MAX` as the size in the job state snapshot, causing incorrect pack grouping.

**Fix approach:** Check `ec` after the `file_size` call: `if (ec) { return std::nullopt; }`.

## Security Considerations

### Subprocess Command String Injection

**Risk:** `buildCMD()` methods construct shell command strings via string concatenation. On POSIX platforms, `boost::process::v1::child` may parse the command through `/bin/sh` when given a single string. File paths are double-quoted but this is insufficient against paths containing `"` characters.

**Files:**
- `src/video/encode_config.h:81-109`
- `src/picture/picture_compress.cpp:27-34`
- `src/utils/utils.cpp:27-168` (subprocess execution)

**Current mitigation:** Double-quoting of file paths; `CreateProcess` on Windows avoids shell interpretation.

**Recommendations:**
1. Switch to argument-vector subprocess invocation: `bp::child(exe, arg1, arg2, ...)`
2. Validate file paths to reject shell-metacharacter characters (`"`, `$`, backtick, `;`, `|`, `&`)
3. Resolve executable paths to absolute form and validate they reside in expected directories

### PATH Injection for FFmpeg Discovery

**Risk:** `findFFmpeg()` and `findFFprobe()` discover binaries via bare name execution (`exec2("ffmpeg -version")`), which resolves from PATH. A PATH-controlled attacker can substitute a malicious binary.

**Files:** `src/utils/utils.cpp:338,317`

**Current mitigation:** Users can specify `--ffmpeg-install-dir` to avoid PATH entirely. Default behavior falls back to PATH.

**Recommendations:** Resolve the absolute path of the found binary using `boost::process::search_path()` and store in `ToolchainPaths`. Never pass unqualified binary names to `buildCMD()`.

### Global Detached Thread in Stop-Signal Handler

**Issue:** `stop_signal.cpp:39-54` detaches a watchdog thread that runs indefinitely and can only exit via `::ExitProcess`. If a future change prevents `ExitProcess`, the thread leaks silently.

**Files:** `src/infra/stop_signal.cpp:39-54`

**Current mitigation:** The thread terminates only via `ExitProcess`, which the signal handler guarantees will be called. This is currently safe but fragile.

**Recommendations:** Store the watchdog `std::jthread` in a global variable so it can be explicitly joined during graceful shutdown.

### Environment Variable Reading Without Sanitization

**Issue:** Multiple modules read environment variables (`LOCALAPPDATA`, `APPDATA`, `HOME`, `COLUMNS`) and use values directly for filesystem operations. No validation against path traversal is performed.

**Files:** `src/app/prelude.cpp:43-50`, `src/infra/terminal.cpp:41-56`, `src/infra/console_width.cpp:20-39`

**Current mitigation:** Paths are appended with `/encro/logs`, so `..` traversal from the env var is partially mitigated.

**Recommendations:** Validate that resolved environment variable paths are absolute and do not contain `..` components.

## Performance Bottlenecks

### Monitor Loop Polls at 50 Hz (20ms)

**Problem:** `startEncodingMonitor()` in `src/video/video_batch_execution.cpp` polls at 20ms intervals (50 Hz). Terminal progress bars need only ~10 Hz for smooth visual updates. This wastes CPU.

**Files:** `src/video/video_batch_execution.cpp` (monitor loop timing)

**Improvement path:** Per Phase 3 of the perf plan: change to 100ms (10 Hz), optionally adaptive (50ms first 5 seconds, 200ms thereafter).

### WebP Adaptive Encoding Always Starts at Quality 80

**Problem:** `encodeWebpWithTargetSize()` always starts at `quality = 80` regardless of input file size. Large files waste 1-3 ffmpeg passes before reaching target size.

**Files:** `src/video/video_encode_runner.cpp:201`

**Improvement path:** Per Phase 5 of the perf plan: read input file size first, start at quality 50 for >100 MB, 40 for >200 MB.

### Sequential Encoding Path Uses `immer::map`

**Problem:** `runEncodingWithoutProgress()` builds results using `immer::map::set()` in a sequential loop, creating a new persistent tree node per insertion.

**Files:** `src/video/video_batch_execution.cpp`

**Improvement path:** Per Phase 7 of the perf plan: accumulate in `std::unordered_map`, convert to `immer::map` at the end.

### `ffprobe` Scans Before Job-State Filtering

**Problem:** `finalizeVideoList()` launches parallel ffprobe on all scanned candidates before checking job-state for already-completed tasks. Some ffprobe work is wasted on resume.

**Files:** `src/video/video_info.cpp` (via orchestration in `video_process.cpp`)

**Improvement path:** Move job-state filtering before ffprobe scanning in the orchestration flow.

## Fragile Areas

### `EncodeConfig` Validation/Construction Split

**Files:** `src/video/encode_config.h`

**Why fragile:** All fields are `std::optional`. `validate()` checks consistency but `buildCMD()`, `buildOutputPath()`, and `buildOutputFileName()` will throw (not return `eh::Result`) if `inputPath` is absent. If validation is skipped or config is modified post-validation, the throw propagates without structured error context.

**Safe modification:** Always call `validate()` before any command-building method. Do not add new optional fields without adding corresponding validation.

**Test coverage:** `tests/video/encode_config_tests.cpp` exercises validation; construction error paths are not covered.

### `EncodingState` Manual Mutex Management

**Files:**
- `src/core/app_context.h:67-86` (struct definition with raw `std::mutex mtx`)
- `src/video/video_batch_execution.cpp` (all accessors)

**Why fragile:** Every access site must manually acquire `EncodingState::mtx`. No encapsulation enforces locking. The `lastProgressAtomic` field was added as a parallel atomic to avoid mutex contention for progress reads, creating a dual-access protocol (some fields read under mutex, others via atomic). Adding a new field requires auditing every access site.

**Safe modification:** Do not add fields to `EncodingState` without finding all lock and unlock sites. Prefer an accessor method that encapsulates the locking pattern.

**Test coverage:** Thread safety is not directly tested. Orchestration tests exercise happy path only.

### `StopSignal` Global State Leaking Between Tests

**Files:** `src/infra/stop_signal.cpp`, `tests/test_utils.h` (via `ScopedStopSignalReset`)

**Why fragile:** `stopsignal::isStopRequested()` reads a global atomic. The `ScopedStopSignalReset` RAII guard resets it between tests. If a test forgets the guard, stop-signal state can leak between test cases, causing flaky failures in encoding orchestration tests.

**Safe modification:** Always use `ScopedStopSignalReset` in tests that check stop signals. Consider a test fixture that installs this guard automatically.

### `immer::atom` Usage Without Contention Strategy

**Files:**
- `src/video/video_batch_execution.cpp:70` (`immer::atom<SharedSnapshot>`)
- `src/core/app_context.h:96-113` (`immer::atom<VideoInfoCacheMap>`)

**Why fragile:** `immer::atom` uses CAS loops internally. Under contention, CAS retry loops degrade performance. Currently low contention (one writer per atom), but adding concurrent writers would change this.

**Safe modification:** Ensure each `immer::atom` has a single writer. Document the writer thread explicitly if adding a second mutation path.

## Scaling Limits

### No Atomic Zip File Creation

**Issue:** `libzippp` opens zip files for append. If zip creation fails mid-write, a partial zip file remains on disk and could be mistaken for a valid archive.

**Files:** `src/pack/packer.cpp` (zip operations via `libzippp`)

**Fix approach:** Write zips to a temporary name and rename atomically on success. Remove temp file on failure.

### No Validation on `maxParallelJobs == 0`

**Issue:** `AppConfig::maxParallelJobs` (`src/core/app_context.h:51`) can be set to `0`. This causes `std::thread` constructor failures or an unhandled 0-worker pool.

**Files:** `src/core/app_context.h:51`, `src/core/task_executor.cpp`

**Fix approach:** Validate in `buildConfig` (`src/cmd/config_builder.cpp`) that `maxParallelJobs >= 1`, or clamp at consumption site.

## Dependencies at Risk

### `libzippp` -- Verify Upstream Maintenance

**Risk:** `libzippp` wraps `libzip` for C++ convenience. If `libzippp` becomes unmaintained or lags behind `libzip` API changes, the pack workflow needs a replacement.

**Migration plan:** Monitor `libzippp` release cadence. If abandoned, migrate to `libzip` C API directly wrapped in internal C++ helpers.

### `boost::process::v1` -- API Transition Risk

**Risk:** The `boost::process::v1` namespace suggests a transitional API. Future Boost releases may remove `v1` in favor of `v2`.

**Impact:** `src/utils/utils.cpp` is the sole subprocess execution layer. If `v1` is removed, the entire encoding pipeline breaks.

**Migration plan:** Monitor Boost.Process release notes. Prepare to migrate to `boost::process::v2` or wrap the process API behind an internal interface.

### `indicators` -- Progress Bar API Instability

**Risk:** `indicators` is actively maintained but has a history of API-breaking changes between minor versions.

**Impact:** Progress bar code in `src/core/progress.cpp` and `src/video/video_batch_execution.cpp` uses `indicators` types directly. An incompatible update requires significant rework.

**Migration plan:** Pin the `indicators` version in xmake. Wrap `indicators` usage behind `progress::ProgressContext` so only one module needs updating.

## Test Coverage Gaps

### E2e Tests Unbuildable

**What's not tested:** Full end-to-end pipeline: subprocess execution, job-state recovery, multi-file orchestration, pack workflows.

**Files:** `tests/e2e/encro_e2e_tests.cpp`, `tests/e2e/e2e_test_utils.h`

**Risk:** High. Integration regressions in encoding orchestration, job-state persistence, and pack workflows are not caught by unit tests.

**Priority:** High. Fix the build by adding `src` to `e2e_tests` include directories in `xmake.lua`.

### Thread Safety Not Covered by Tests

**What's not tested:** `EncodingState` mutex protocol, progress monitor thread lifecycle, `stop_signal` watch thread, concurrent `immer::atom` access.

**Files:** `src/video/video_batch_execution.cpp`, `src/core/app_context.h`, `src/infra/stop_signal.cpp`

**Risk:** Medium. Thread safety bugs manifest as rare, hard-to-reproduce crashes. No automated concurrency tests exist.

**Priority:** Medium. Add targeted concurrency tests using `std::latch`/`std::barrier`.

### Permission/I/O Error Paths in Packer

**What's not tested:** Directory scanning with permission-denied entries, disk-full during zip writing, file-locked source files.

**Files:** `src/pack/packer.cpp:682-691`

**Risk:** Medium. Common real-world failure modes for a file-packing utility. Current behavior is throw-then-crash.

**Priority:** Medium. Fix the iterator to use `skip_permission_denied` and add tests with mock filesystem operations.

### `EncodeConfig` Construction Error Paths

**What's not tested:** Building a command from `EncodeConfig` with `inputPath` absent. The throw paths in `buildCMD()`, `buildOutputPath()`, `buildOutputFileName()` are not exercised.

**Files:** `src/video/encode_config.h:49, 71, 86`

**Risk:** Low. Callers always validate first, so these paths rarely trigger in practice.

**Priority:** Low. Add tests after converting `throw` to `eh::Result`.

---

*Concerns audit: 2026-04-26*
