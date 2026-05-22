<!-- refreshed: 2026-05-22 -->
<!-- last_mapped_commit: 45d19cdfd54971db03ee0758ccaad6c61aad4ff1 -->
# Architecture

**Analysis Date:** 2026-05-22

## System Overview

```text
┌─────────────────────────────────────────────────────────────────────────┐
│                           main()                                         │
│                        `src/main.cpp`                                    │
└───────────────────────────┬─────────────────────────────────────────────┘
                            │ crash::installHandlers()
                            ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                      appentry::run()                                     │
│                   `src/app/app_entry.cpp`                                │
│    Parse CLI → Build config → Resolve toolchain → Run pipeline           │
└───────────────────────────┬─────────────────────────────────────────────┘
                            │
               ┌────────────┼──────────────┐
               ▼            ▼              ▼
     runVideo()      runPicture()    runPackOnly()
     `video/`        `picture/`      (pack-only shortcut)
         │
         ├─────────────┬─────────────────┬──────────────────┐
         ▼             ▼                 ▼                  ▼
  video_info      video_process    video_output_      video_encode_
  (ffprobe)       (dispatch)       planning            runner (ffmpeg)
         │             │                 │                  │
         └─────────────┴──────┬──────────┴──────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    video_batch_execution                                  │
│                 `src/video/video_batch_execution.cpp`                    │
│    Parallel encode + progress bars + progress monitoring thread          │
└───────────────────────────┬─────────────────────────────────────────────┘
                            │ (optional packOutput)
                            ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                          pack::execute()                                  │
│                     `src/pack/pack.cpp`                                   │
│          PackRequest → PackPlan → PackService → Packer → libzippp        │
└───────────────────────────┬─────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                     Zip archives on disk (libzippp)                       │
└─────────────────────────────────────────────────────────────────────────┘
```

## Component Responsibilities

| Component | Responsibility | File |
|-----------|----------------|------|
| `main()` | Install crash handlers; catch unhandled exceptions | `src/main.cpp` |
| `appentry::run()` | CLI-driven top-level app lifecycle: parse, config, toolchain, pipeline | `src/app/app_entry.cpp` |
| `prelude::initStartup()` | CLI parsing + verbose logging setup + terminal color config | `src/app/prelude.cpp` |
| `cmd::commandLineInit()` | CLI11-based argument parsing into `CmdParseResult` | `src/cmd/cmd.cpp` |
| `cmd::buildConfig()` | Convert `CmdParseResult` into validated `appctx::AppConfig` | `src/cmd/config_builder.cpp` |
| `pipeline::run()` | Top-level routing: video vs. picture vs. pack-only | `src/app/pipeline.cpp` |
| `toolchain::resolve()` | Locate ffmpeg/ffprobe on PATH or via `--ffmpeg-path` | `src/infra/toolchain.cpp` |
| `video_process` | Dispatch encoding by input type (single file, multi file, directory) | `src/video/video_process.cpp` |
| `video_info` | ffprobe wrapper: codec info, frame counts, HEVC detection | `src/video/video_info.cpp` |
| `video_output_planning` | Compute output file paths with optional `Keep` layout mirroring directory structure | `src/video/video_output_planning.cpp` |
| `video_encode_runner` | ffmpeg subprocess invocation with progress pipe | `src/video/video_encode_runner.cpp` |
| `video_batch_execution` | Thread pool + progress bars + progress-monitor thread | `src/video/video_batch_execution.cpp` |
| `video_progress_parser` | Parse ffmpeg `-progress` output for frame count | `src/video/video_progress_parser.cpp` |
| `EncodeConfig` | Build ffmpeg CLI command strings (codec, CRF, WebP) | `src/video/encode_config.h` |
| `picture_process` | Picture workflow: scan images, compress to WebP, pack to ZIP | `src/picture/picture_process.cpp` |
| `picture_compress` | Image compression via ffmpeg WebP encoder, batch mode | `src/picture/picture_compress.cpp` |
| `pack::execute()` | Public pack API: accepts `PackRequest`, returns `PackRunResult` | `src/pack/pack.cpp` |
| `pack::Packer` | Low-level ZIP creation, file grouping by size/dir | `src/pack/packer.cpp` |
| `pack::PackService` | High-level pack orchestration with progress, naming, summaries | `src/pack/pack_service.cpp` |
| `jobstate::Store` | JSON-backed persistable task state for resume after interruption | `src/core/job_state.cpp` |
| `taskexec::runTasks()` | Generic parallel task executor with progress bars | `src/core/task_executor.cpp` |
| `parallel::runIndexedTasks()` | Simple indexed parallel loop abstraction | `src/core/parallel.cpp` |
| `media::scanByExtensions()` | Recursive file scanner filtered by extension | `src/core/media_scanner.cpp` |
| `progress::ProgressContext` | Thread-safe multi-bar progress indicator Manager | `src/core/progress.cpp` |
| `collisionnaming` | Conflict-safe flat file naming via FNV-1a hash prefixing | `src/core/collision_naming.h` |
| `stopsignal` | SIGINT/Ctrl+C handler with atomic flag | `src/infra/stop_signal.cpp` |
| `crash` | SEH (Windows) + C++ terminate handler + stacktrace capture | `src/infra/crash_runtime.cpp` |
| `terminal` | Styled console output: colors, badges, path rendering | `src/infra/terminal.cpp` |
| `utils::exec2()` | Subprocess execution with optional line-by-line output callback | `src/utils/utils.cpp` |

## Pattern Overview

**Overall:** Pipeline-then-executor pattern with a single mutable context struct.

**Key Characteristics:**
- **Mutable context passing:** `appctx::AppContext` is a single struct (`AppConfig` + `ToolchainPaths` + `RuntimeContext`) passed by mutable reference through the entire call chain. No dependency injection framework; everything receives `AppContext&`.
- **Result types, not exceptions:** Operational failures use `eh::Result<T>` (alias for `std::expected<T, std::string>`). Exceptions are reserved for truly unrecoverable errors (caught in `main` via `crash::reportCaughtException`).
- **Thread-safe shared state via immer:** `RuntimeContext::VideoInfoCacheStore` uses `immer::atom<immer::map<...>>` for lock-free read-heavy caching of ffprobe results. `EncodingProgressState` uses `immer::atom` for active-slot snapshots.
- **JSON-backed resume:** `jobstate::Store` persists task records as JSON to `%LOCALAPPDATA%/encro/state/` (or custom path), supporting graceful resume after interruption.
- **Progress everywhere:** `progress::ProgressContext` wraps `indicators::DynamicProgress<ProgressBar>`, used by video encoding, picture compression, and pack operations with a consistent `Tone` enum for color coding.
- **Free functions over classes:** Most logic lives in free functions within namespaces. Only `Packer`, `Store`, `ProgressContext`, and `ProgressState` (in `video_encoding_state.cpp`) are classes. No inheritance hierarchies.

## Layers

### Entry Point Layer (`src/main.cpp`, `src/app/`)
- **Purpose:** App lifecycle: crash handlers, CLI parse, config build, toolchain discovery, pipeline dispatch.
- **Location:** `src/main.cpp`, `src/app/app_entry.cpp`, `src/app/prelude.cpp`, `src/app/pipeline.cpp`
- **Contains:** `main()`, `appentry::run()`, `prelude::initStartup()`, `pipeline::run()`
- **Depends on:** `cmd/`, `core/`, `infra/`, `video/`, `picture/`, `pack/`
- **Used by:** Nothing (top-level). Called only from `main()`.

### CLI/Config Layer (`src/cmd/`)
- **Purpose:** Parse CLI arguments via CLI11, build validated `AppConfig`.
- **Location:** `src/cmd/cmd.cpp`, `src/cmd/config_builder.cpp`
- **Contains:** `commandLineInit()`, `CmdParseResult`, `buildConfig()`
- **Depends on:** `core/app_context.h`, `core/error_handle.h`
- **Used by:** `prelude::initStartup()`, `appentry::run()`

### Core/Abstractions Layer (`src/core/`)
- **Purpose:** Domain types, shared state, reusable executors, media scanning, error handling.
- **Location:** `src/core/`
- **Contains:** `AppContext` (config + toolchain + runtime), `ProgressContext`, `jobstate::Store`, `taskexec::runTasks()`, `parallel::runIndexedTasks()`, `media::scanByExtensions()`, `collisionnaming`, `displaytext`, `pathroots`
- **Depends on:** Third-party libs (Boost, indicators, spdlog, immer)
- **Used by:** Every layer above (app, video, picture, pack, infra)

### Processing Layer (`src/video/`, `src/picture/`)
- **Purpose:** Domain-specific processing: video encoding (ffmpeg), picture compression (WebP), output planning, progress monitoring.
- **Location:** `src/video/`, `src/picture/`
- **Contains:** `video_process`, `video_batch_execution`, `video_encode_runner`, `video_info`, `video_output_planning`, `video_progress_parser`, `picture_process`, `picture_compress`
- **Depends on:** `core/`, `infra/`, `utils/`, `pack/` (for pack-output workflows)
- **Used by:** `pipeline::run()`

### Pack/ZIP Layer (`src/pack/`)
- **Purpose:** Create ZIP archives with file grouping, naming strategies, resumable execution.
- **Location:** `src/pack/`
- **Contains:** `pack::execute()` (public API), `Packer` (low-level ZIP), `PackService` (orchestration)
- **Depends on:** `core/`, `infra/`, `libzippp`
- **Used by:** `pipeline::run()`, `video_process` (packOutput option), `picture_process`

### Infrastructure Layer (`src/infra/`)
- **Purpose:** Platform services: crash handling, terminal styling, signal handling, toolchain discovery.
- **Location:** `src/infra/`
- **Contains:** `crash`, `terminal`, `stopsignal`, `toolchain`, `stacktrace`, `console_width`, `processenv`
- **Depends on:** OS APIs (Windows: dbghelp, SEH; POSIX: signal, dl), fmt, spdlog
- **Used by:** All layers

### Utilities Layer (`src/utils/`)
- **Purpose:** Cross-cutting utilities: subprocess execution, ffmpeg/ffprobe discovery, UUID generation.
- **Location:** `src/utils/utils.cpp`
- **Contains:** `exec2()`, `readUserIpt()`, `findFFmpeg()`, `findFFprobe()`, `getUUID()`
- **Depends on:** Boost.Process (legacy), OS APIs
- **Used by:** `video/`, `picture/`, `infra/toolchain`

## Data Flow

### Primary Request Path (Video Encoding)

1. `main()` calls `appentry::run(argc, argv)` (`src/main.cpp:9`)
2. `appentry::run` calls `prelude::initStartup()` to parse CLI (`src/app/app_entry.cpp:167`)
3. `cmd::buildConfig()` converts `CmdParseResult` to `AppConfig` (`src/app/app_entry.cpp:117`)
4. `toolchain::resolve()` locates ffmpeg/ffprobe, populates `ToolchainPaths` (`src/app/app_entry.cpp:132`)
5. `pipeline::run(ctx)` dispatches to `runVideo()` (`src/app/pipeline.cpp:144`)
6. `runVideo()` checks input type: single file vs. multi file vs. directory (`src/app/pipeline.cpp:106-113`)
7. `handlePathEncoding()` scans directory via `readAllVids()`, then calls `runEncodingTasks()` (`src/video/video_process.cpp`)
8. `video_batch_execution::runEncodingTasks()` creates `EncodingProgressState` (progress bars) and launches worker threads via `parallel::runIndexedTasks()` (`src/video/video_batch_execution.cpp`)
9. Each worker calls `video_encode_runner::encodeVideo()` which builds ffmpeg command via `EncodeConfig::buildCMD()` and runs it via `utils::exec2()` with a progress-file callback (`src/video/video_encode_runner.cpp`)
10. A monitor thread (`startEncodingMonitor`) periodically reads ffmpeg progress files via `video_progress_parser::parseProgressFile()` and updates progress bars (`src/video/video_encoding_state.cpp:75`)
11. On completion, if `packOutput` is enabled, `pack::execute()` is called on the output directory (`src/video/video_process.cpp`)

### Picture Processing Flow

1. `pipeline::run()` dispatches to `runPicture()` (`src/app/pipeline.cpp:150`)
2. `runPicturePackWorkflow()` scans images via `readAllPics()`, compresses to WebP via `compressImageBatch()`, then packs to ZIP via `packAllPicsToZip()` (`src/picture/picture_process.cpp`)
3. `compressImageBatch()` runs ffmpeg WebP conversions in parallel using `parallel::runIndexedTasks()` (`src/picture/picture_compress.cpp`)
4. `packAllPicsToZip()` uses `Packer::buildDirectoryPackPlan()` + `pack::execute()` (`src/picture/picture_process.cpp`)

### Pack-Only Flow

1. `pipeline::run()` dispatches to `runPackOnly()` (`src/app/pipeline.cpp:137-141`)
2. `runPackOnly()` validates input is a directory, then calls `pack::execute()` with `PackMode::Directory` (`src/app/pipeline.cpp:82`)
3. `pack::execute()` internally creates a `Packer`, builds a `PackPlan`, and orchestrates via `PackService` (`src/pack/pack.cpp`)

### Resume/Restore Flow

1. `shouldEnableJobState()` returns true for video processing or when `--resume`/`--restart`/`--state-file` is set (`src/app/pipeline.cpp:29-33`)
2. `ensureJobState()` creates a `jobstate::Store` from the state file path, calls `initialize()` which loads existing snapshot or creates new (`src/app/pipeline.cpp:35-49`)
3. During encoding, `jobstate::Store` receives progress updates via `markProgress()`, `markSucceeded()`, `markFailed()` calls from the batch execution monitor (`src/video/video_encoding_state.cpp:126-172`)
4. On re-run with `--resume`, `mergeTasks()` identifies already-succeeded tasks and skips them (`src/core/job_state.cpp`)

**State Management:**
- `AppContext` is the sole mutable state carrier — created on the stack in `appentry::run()`, passed by `&` to all subsystems.
- `RuntimeContext::videoInfoCache` uses lock-free `immer::atom` for concurrent reads of ffprobe data.
- `jobstate::Store` uses `std::mutex` for thread-safe JSON read/write, flushing periodically.
- `EncodingState` (`appctx::EncodingState`) is a shared_ptr-based per-video state with `std::mutex` for field access.

## Key Abstractions

### `appctx::AppContext`
- **Purpose:** Single aggregate context struct carrying all mutable state through the call chain.
- **Location:** `src/core/app_context.h:119-124`
- **Pattern:** Plain struct with three sub-structs: `AppConfig` (user-specified options), `ToolchainPaths` (resolved ffmpeg/ffprobe locations), `RuntimeContext` (video info cache + job state pointer).
- **Usage:** Passed as `appctx::AppContext&` to every function that needs configuration or mutable runtime state.

### `eh::Result<T>`
- **Purpose:** Typed result-or-error monad. Replaces exceptions for all operational failures.
- **Location:** `src/core/error_handle.h:9-17`
- **Pattern:** `std::expected<T, std::string>`. Error creation via `eh::makeError("fmt", args...)`. Propagation via early return: `if (!res) return eh::makeError("...: {}", res.error());`

### `pack::PackRequest`
- **Purpose:** Single self-contained request struct for archive creation. All pack operations go through this.
- **Location:** `src/pack/pack.h:74-91`
- **Pattern:** Uses C++20 designated initializers. Optional fields with sensible defaults. Contains: entries, mode, outputDir, naming, grouping, job state pointer, optional callbacks.

### `EncodeConfig`
- **Purpose:** Immutable encode configuration that validates inputs and builds ffmpeg CLI commands.
- **Location:** `src/video/encode_config.h:12-110`
- **Pattern:** Plain struct with `validate()` returning `eh::Result<void>` and `buildCMD()` returning the ffmpeg command string. Supports `mp4` (HEVC) and `webp` output formats.

### `video_batch_execution::detail::EncodingExecutionContext`
- **Purpose:** Shared mutable state for the video encoding worker pool + progress monitor thread.
- **Location:** `src/video/video_batch_execution.h:134-275`
- **Pattern:** Ref-struct wrapping `AppContext&`, `EncodingProgressState&`, planned output files, and action IDs. Contains all progress-bar update methods (`barEncodingStart`, `barEncodingStatus`, `updateOverall`, `finalizeState`).

### `jobstate::Store`
- **Purpose:** JSON-backed persistent task state for resumable execution.
- **Location:** `src/core/job_state.h:72-129`
- **Pattern:** Class with `initialize()` (load/create), `mergeTasks()` (reconcile planned vs. stored), per-task status mutation methods (`markRunning`, `markSucceeded`, `markFailed`, `markInterrupted`), thread-safe `flush()`.

## Entry Points

### `main()`
- **Location:** `src/main.cpp:6`
- **Triggers:** Process entry point. Called by OS/CRT.
- **Responsibilities:** Install crash handlers (SEH + terminate), wrap `appentry::run()` in try/catch for catastrophic exception reporting.

### `appentry::run()`
- **Location:** `src/app/app_entry.cpp:162`
- **Triggers:** Called by `main()`.
- **Responsibilities:** Install stop-signal handler, orchestrate full lifecycle: parse CLI, handle --help/--version, build config, verify toolchain, run pipeline.

### `pipeline::run()`
- **Location:** `src/app/pipeline.cpp:129`
- **Triggers:** Called by `appentry::run()`.
- **Responsibilities:** Initialize job state if needed, then dispatch to `runVideo()`, `runPicture()`, or `runPackOnly()` based on `config.processType` and `config.packOnly`.

### `pack::execute()`
- **Location:** `src/pack/pack.cpp`
- **Triggers:** Called by pipeline (pack-only or pack-output), or directly in tests.
- **Responsibilities:** Accept `PackRequest`, create `Packer` + `PackService` internals, return `eh::Result<PackRunResult>`.

### `taskexec::runTasks()`
- **Location:** `src/core/task_executor.cpp`
- **Triggers:** Called by `pack::PackService` for parallel ZIP creation.
- **Responsibilities:** Execute a `TaskPlan` (list of `TaskSpec` with labels + run functions) with configurable max concurrency and integrated progress bars. Returns per-task results.

### `parallel::runIndexedTasks()`
- **Location:** `src/core/parallel.cpp`
- **Triggers:** Called by video batch execution and picture compress for parallel encoding.
- **Responsibilities:** Execute `N` indexed tasks across `W` worker threads using `thread-pool`.

## Architectural Constraints

- **Threading:** Concurrent encoding uses `parallel::runIndexedTasks()` backed by `thread-pool`. A separate `std::jthread` runs the progress monitor. Shared mutable state uses `immer::atom` (lock-free) or `std::atomic` + `std::mutex`. No raw `std::thread` management outside `parallel` and the monitor.
- **Global state:** `processenv` namespace has inline functions only (stateless). `terminal` namespace manages a global color mode via module-local static (set once at startup). `spdlog` has a global logger instance set once in `prelude::setupLogging()`. `stopsignal` uses atomic globals for the signal flag — reset between tests.
- **Circular imports:** Not detected. Dependencies flow unidirectionally: `app` -> `cmd`/`video`/`picture`/`pack`/`core`/`infra` -> `third-party`. No reverse dependencies.
- **External tool dependency:** ffmpeg and ffprobe binaries must be present on user PATH or provided via `--ffmpeg-path`. The app does not bundle them. Missing tools cause `toolchain::resolve()` to fail early with a descriptive error.

## Anti-Patterns

### Boost.Process legacy dependency

**What happens:** `src/utils/utils.cpp` uses Boost.Process for subprocess execution (`exec2()`) while the project is in the process of migrating away from Boost-specific APIs. The xmake.lua lists Boost as `boost[all]` and references that large monolithic dependency mainly for process and JSON.

**Why it's wrong:** Pulls in the entire Boost distribution when only a subset is needed. Adds compilation time and binary size overhead.

**Do this instead:** Replace `boost::process` with a lighter subprocess library or platform-native APIs, as was done for program_options (migrated to CLI11 in v1.6). The `boost::json` usage is reasonable (header-only).

### Video encoding progress monitor busy-polls

**What happens:** `monitorEncodingProgress()` in `src/video/video_encoding_state.cpp` polls ffmpeg progress files every 20ms in a tight loop with `std::this_thread::sleep_for(20ms)`, even when no encoding is active. It breaks only when `finished >= overallTotal` or on cancel signal with no active tasks.

**Why it's wrong:** Wastes CPU cycles during idle periods. The sleep is hardcoded at 20ms regardless of workload.

**Do this instead:** Use a condition_variable or event-based notification from encoding workers. Increase sleep duration when no active tasks exist. Consider an exponential backoff during idle.

## Error Handling

**Strategy:** `std::expected<T, std::string>` for all operational failures. Exceptions only for catastrophic/unrecoverable errors (caught in `main.cpp`).

**Patterns:**
- All domain functions return `eh::Result<T>` or `eh::Result<void>`. The caller checks with `if (!result)` and propagates with `eh::makeError("context: {}", result.error())`.
- `EncodeConfig::validate()` returns `eh::Result<void>` — called before command construction.
- `crash::installHandlers()` sets SEH (Windows structured exception handler) and `std::set_terminate` for unhandled C++ exceptions. Reports via `spdlog` and prints stacktrace.
- `stopsignal::installHandler()` catches SIGINT/Ctrl+C and sets an atomic flag. Long-running operations check `stopsignal::isStopRequested()` periodically and return `kCanceledExitCode` (130).

## Cross-Cutting Concerns

**Logging:** `spdlog` with async thread pool (8192 queue, 1 worker thread). Verbose mode (`--verbose`) writes to `%LOCALAPPDATA%/encro/logs/encro.verbose.log`. Optional echo to stdout (`--verbose-echo`). Non-verbose mode sets log level to `off`. Flush-on-error policy.

**Validation:** CLI11 validates argument types at parse time. `EncodeConfig::validate()` checks format/codec/CRF ranges. `pack::execute()` validates output dir writability. `jobstate::Store::initialize()` validates state file JSON structure and `configMatches()` for consistency.

**Authentication:** Not applicable (local CLI tool, no network services).

**Progress Display:** Every long-running operation (encoding, packing, picture compression) creates a `progress::ProgressContext` with color-coded bars via the `Tone` enum: `Idle`, `Active`, `Overall`, `Success`, `Failure`, `Packing`, `Finalizing`. Video encoding shows per-worker bars + optional overall bar. Packing shows per-archive bars in compact mode or detailed bars in full mode (`--full-progress`).

---

*Architecture analysis: 2026-05-22*
