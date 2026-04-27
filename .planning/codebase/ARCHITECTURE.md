<!-- refreshed: 2026-04-28 -->
# Architecture

**Analysis Date:** 2026-04-28

## System Overview

```text
┌─────────────────────────────────────────────────────────────────────────┐
│                          Application Layer                              │
│  `src/app/`                                                             │
│  ┌──────────────┬──────────────────┬───────────────────────────────────┐│
│  │  app_entry   │     prelude      │            pipeline               ││
│  │  Bootstrap   │  Logging setup,  │  Dispatch to video/picture/pack   ││
│  │  & orchestrate│ config summary  │  workflows                        ││
│  └──────┬───────┴────────┬─────────┴───────────┬───────────────────────┘│
└─────────┼────────────────┼─────────────────────┼────────────────────────┘
          │                │                     │
          ▼                ▼                     ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        Domain Layer                                      │
│  ┌──────────┐  ┌──────────────┐  ┌──────────────────────────────────┐  │
│  │  video/  │  │  picture/    │  │           pack/                   │  │
│  │  Encode  │  │  Compress    │  │  ZIP archive creation & planning  │  │
│  │  & batch │  │  & pack      │  │                                   │  │
│  └────┬─────┘  └──────┬───────┘  └──────────────┬────────────────────┘  │
└───────┼────────────────┼────────────────────────┼───────────────────────┘
        │                │                        │
        ▼                ▼                        ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                          Core Layer                                      │
│  `src/core/`                                                             │
│  ┌────────────┬──────────────┬────────────┬────────────┬───────────────┐│
│  │ appctx     │  jobstate    │ taskexec   │  progress  │ arc/collision ││
│  │ Config &   │  Resumable   │ Parallel   │  Terminal  │ Archive plans ││
│  │ shared ctx │  job state   │ task runner│  bars      │ & naming      ││
│  └────────────┴──────────────┴────────────┴────────────┴───────────────┘│
└─────────────────────────────────────────────────────────────────────────┘
          │                │                     │
          ▼                ▼                     ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                       Infrastructure Layer                               │
│  `src/infra/`                │              `src/utils/`                 │
│  ┌──────────┬──────────────┐ │  ┌───────────────────────────────────┐  │
│  │ terminal │ crash_rt     │ │  │ exec2()         findFFmpeg()       │  │
│  │ Color I/O│ Crash/except │ │  │ readUserIpt()   getUUID()          │  │
│  ├──────────┼──────────────┤ │  │ getParamStr()                      │  │
│  │toolchain │ stop_signal  │ │  └───────────────────────────────────┘  │
│  │FFmpeg    │ Ctrl+C /     │ │                                          │
│  │discovery │ graceful stop│ │                                          │
│  ├──────────┼──────────────┤ │                                          │
│  │stacktrace│console_width │ │                                          │
│  └──────────┴──────────────┘ │                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

## Component Responsibilities

| Component | Responsibility | File |
|-----------|----------------|------|
| Entry point | Install crash handlers, catch all exceptions, delegate to `appentry::run` | `src/main.cpp` |
| App entry | Orchestrate startup: parse CLI, build config, resolve toolchain, run pipeline | `src/app/app_entry.cpp` |
| Prelude | Setup spdlog logging, collect startup context, print config summary | `src/app/prelude.cpp` |
| Pipeline | Dispatch request to correct workflow (video/picture/pack-only) | `src/app/pipeline.cpp` |
| CLI parsing | Define Boost.ProgramOptions, parse args into `variables_map` | `src/cmd/cmd.cpp` |
| Config builder | Translate `variables_map` into typed `AppConfig` with validation | `src/cmd/config_builder.cpp` |
| App context | Central config + toolchain paths + runtime state container | `src/core/app_context.h` |
| Job state store | Resumable job state: persists tasks, supports merge/restart/interrupt | `src/core/job_state.cpp` |
| Task executor | Generic parallel task runner with progress bar per slot | `src/core/task_executor.cpp` |
| Progress | Terminal progress bars via `indicators` library, cursor guard | `src/core/progress.cpp` |
| Media scanner | Scan directories for files by extension | `src/core/media_scanner.cpp` |
| Video process | Video workflow: scan, plan outputs, encode (single/multi/path) | `src/video/video_process.cpp` |
| Video encode runner | Execute single `ffmpeg` encode, parse progress | `src/video/video_encode_runner.cpp` |
| Video batch execution | Parallel encode task orchestration with progress tracking | `src/video/video_batch_execution.cpp` |
| Video info | Extract video metadata via `ffprobe`, filter encodable videos | `src/video/video_info.cpp` |
| Video output planning | Plan output file paths for a batch of input videos | `src/video/video_output_planning.cpp` |
| Encode config | Build `ffmpeg` command-line from structured config | `src/video/encode_config.h` |
| Video progress parser | Parse `ffmpeg -progress` output for frame count & status | `src/video/video_progress_parser.cpp` |
| Picture process | Picture workflow: scan, compress, build pack plan, execute | `src/picture/picture_process.cpp` |
| Picture compress | Image compression via `ffmpeg` to WebP | `src/picture/picture_compress.cpp` |
| Packer | ZIP file creation via `libzippp`, file grouping by size | `src/pack/packer.cpp` |
| Pack service | Declarative `PackPlan` execution with task executor | `src/pack/pack_service.cpp` |
| Terminal I/O | Colored terminal output with `fmt`, stream management | `src/infra/terminal.cpp` |
| Crash runtime | Windows SEH/vectored exception handler + `std::terminate` handler | `src/infra/crash_runtime.cpp` |
| Stop signal | `SIGINT`/`Ctrl+C` handler with atomic flag | `src/infra/stop_signal.cpp` |
| Toolchain | Discover `ffmpeg`/`ffprobe` on system PATH or custom install dir | `src/infra/toolchain.cpp` |
| Path roots | Compute normalized root dir and common ancestor of paths | `src/core/path_roots.h` |
| Collision naming | Conflict-safe flat file naming with FNV-1a hashing | `src/core/collision_naming.h` |
| Display text | Unicode-aware text truncation, display width calculation | `src/core/display_text.h` |
| Archive plan | Prepare resumable pack execution with job state integration | `src/core/archive_plan.cpp` |
| Utilities | Process execution (popen), user prompt, UUID gen, FFmpeg discovery | `src/utils/utils.cpp` |

## Pattern Overview

**Overall:** Layered architecture with dependency inversion via dependency injection (the `AppContext` struct is passed through all layers).

**Key Characteristics:**
- **Central context container (`AppContext`)**: All modules receive a mutable reference to `appctx::AppContext` which holds config, toolchain paths, and runtime state (job state store, video info cache). No global mutable state — the `RuntimeContext` holds all shared runtime structures.
- **Result type (`eh::Result<T>`)**: All fallible operations return `std::expected<T, std::string>`. The `eh::makeError(fmt, args...)` helper creates formatted error strings. Success is checked via `if (!result)`.
- **Immutable data structures for concurrency**: `immer::map` and `immer::atom` are used for thread-safe shared state (video info cache, encoding results map, action ID map). Updates use persistent data structure semantics via `set()` and `update()`.
- **Declarative task/pack plans**: Work is described as data (`TaskPlan`, `PackPlan`) and executed by generic runners (`taskexec::runTasks`, `pack::runPackPlan`).
- **Progress as first-class concern**: The `progress::ProgressContext` wraps `indicators::DynamicProgress` with mutex-protected multi-bar management. Custom `Tone` colors differentiate bar states.
- **Crash resilience**: `crash::installHandlers()` is called before any other logic. Vectored exception handler on Windows, `std::set_terminate`, and `std::set_unexpected` are all installed.

## Layers

**Application Layer (`src/app/`):**
- Purpose: Bootstrap the application, wire dependencies, route to correct workflow
- Location: `src/app/`
- Contains: `app_entry` (orchestration), `prelude` (logging/config summary), `pipeline` (dispatch)
- Depends on: `cmd/`, `core/`, `infra/`, `video/`, `picture/`, `pack/`, `utils/`
- Used by: `src/main.cpp`

**CLI Layer (`src/cmd/`):**
- Purpose: Parse command-line arguments, build typed configuration
- Location: `src/cmd/`
- Contains: `cmd` (Boost.ProgramOptions parsing), `config_builder` (argument → `AppConfig` translation)
- Depends on: `core/` (for `AppConfig`, `AppContext`, `Result`)
- Used by: `src/app/app_entry.cpp`

**Core Layer (`src/core/`):**
- Purpose: Shared domain types, configuration, state management, progress, generic task execution
- Location: `src/core/`
- Contains: `AppContext`, `AppConfig`, `RuntimeContext`, `EncodingState`, `jobstate::Store`, `taskexec`, `progress::ProgressContext`, `media::scanByExtensions`, `parallel::runIndexedTasks`, `collisionnaming`, `pathroots`, `displaytext`, `archiveplan`, `error_handle`
- Depends on: `infra/`, `utils/`, external libs (`immer`, `indicators`, `boost`, `spdlog`)
- Used by: `app/`, `video/`, `picture/`, `pack/`

**Video Domain (`src/video/`):**
- Purpose: Video encoding workflows — scanning, info extraction, output planning, encoding, progress parsing
- Location: `src/video/`
- Contains: `video_process`, `video_batch_execution`, `video_encode_runner`, `video_info`, `video_output_planning`, `encode_config`, `video_progress_parser`, `video_workflow_utils`
- Depends on: `core/`, `infra/`, `utils/`
- Used by: `app/pipeline.cpp`

**Picture Domain (`src/picture/`):**
- Purpose: Picture compression and packing workflows
- Location: `src/picture/`
- Contains: `picture_process`, `picture_compress`
- Depends on: `core/`, `pack/`, `infra/`
- Used by: `app/pipeline.cpp`

**Pack Layer (`src/pack/`):**
- Purpose: ZIP archive creation, file grouping, pack plan execution
- Location: `src/pack/`
- Contains: `packer` (low-level ZIP operations), `pack_service` (high-level plan execution)
- Depends on: `core/`, `libzippp`
- Used by: `app/pipeline.cpp`, `video/video_process.cpp`, `picture/picture_process.cpp`

**Infrastructure Layer (`src/infra/`):**
- Purpose: OS/platform abstractions — terminal I/O, crash handling, toolchain discovery, signal handling, stack traces, console width
- Location: `src/infra/`
- Contains: `terminal`, `crash_runtime`, `toolchain`, `stop_signal`, `stacktrace`, `console_width`
- Depends on: `core/` (for types like `AppConfig`, `ToolchainPaths`, `Result`)
- Used by: all layers

**Utilities (`src/utils/`):**
- Purpose: Generic helpers with no domain knowledge — process execution, user input, UUID generation, FFmpeg/FFprobe path discovery
- Location: `src/utils/`
- Contains: `exec2`, `readUserIpt`, `getUUID`, `findFFmpeg`, `findFFprobe`, `getParamStr`
- Depends on: `boost` (program_options)
- Used by: all layers

## Data Flow

### Primary Request Path (video encoding)

1. **`src/main.cpp`**: `crash::installHandlers()` → `appentry::run(argc, argv)` (line 10)
2. **`src/app/app_entry.cpp`** `run()`: Install stop signal → parse CLI via `commandLineInit` → setup logging via `prelude::initStartup` → validate `--help`/errors → `cmd::buildConfig(vm)` → `toolchain::resolve()` → `pipeline::run(ctx)` (lines 161-179)
3. **`src/app/pipeline.cpp`** `run()`: Dispatch based on `processType` — calls `runVideo(ctx)` for video (line 92)
4. **`src/app/pipeline.cpp`** `runVideo()` → `handlePathEncoding(ctx, inputPath)` (line 57)
5. **`src/video/video_process.cpp`** `handlePathEncoding()`: `scanInputVideos()` → `runScannedEncodingWorkflow()` (line 490)
6. **`src/video/video_process.cpp`** `runScannedEncodingWorkflow()`: `planVideoOutputFiles()` → `prepareEncodeActions()` (with job state merge) → `videobatch::runEncodingTasks()` → optionally `packEncodedVideos()` (lines 266-329)
7. **`src/video/video_batch_execution.cpp`** `runEncodingTasks()`: Build `TaskPlan` with per-file `encodeVideo()` tasks → `taskexec::runTasks(plan)` (parallel execution via `thread-pool`)
8. **`src/video/video_encode_runner.cpp`** `encodeVideo()`: Build `EncodeConfig` → construct `ffmpeg` command → `utils::exec2(cmd, onLine)` with progress file → parse progress → mark job state → return success/failure
9. Outputs written to disk; `AppContext` mutated to track results in `immer::map`

### Picture Pack Workflow

1. **`src/app/pipeline.cpp`** `run()` → `runPicture(ctx)` → `runPicturePackWorkflow(ctx, dirPath)` (line 79)
2. **`src/picture/picture_process.cpp`** `runPicturePackWorkflow()`: `readAllPics()` → optionally compress images via `compressImageBatch()` → `buildPicturePackPlan()` → `pack::runPackPlan(ctx, plan)`
3. **`src/pack/pack_service.cpp`** `runPackPlan()`: Build `TaskPlan` from `PackPlan` groups → `taskexec::runTasks(plan)`
4. **`src/pack/packer.cpp`** `packFilesToZip()`: Create ZIP archive via `libzippp`, add files with progress callback

### Pack-Only Workflow

1. **`src/app/pipeline.cpp`** `runPackOnly()` → `runDirectoryPackWorkflow(ctx, dirPath)` (line 51)
2. **`src/pack/packer.cpp`** `runDirectoryPackWorkflow()`: `buildDirectoryPackPlan()` → `pack::runPackPlan(ctx, plan)`

### Job State Resume Flow

1. **`src/app/pipeline.cpp`** `run()`: `shouldEnableJobState()` checks flags → `ensureJobState()` creates `jobstate::Store`
2. **`src/core/job_state.cpp`** `initialize()`: Load JSON state file → detect config match → return `true` if resuming
3. **`src/video/video_process.cpp`** `prepareEncodeActions()`: `store->mergeTasks(planned)` → return only tasks that `needsExecution()` → skip already-completed tasks

**State Management:**
- `AppContext` struct is created on the stack in `appentry::run()` and passed by mutable reference through all layers. No global mutable state.
- `RuntimeContext` holds shared runtime objects: `JobState` (`shared_ptr<Store>`), `VideoInfoCache` (`immer::atom<immer::map<...>>`)
- Concurrent state uses `immer` persistent data structures or `std::mutex`-protected sections

## Key Abstractions

**`AppContext`:**
- Purpose: Central dependency container bundling config, toolchain, and runtime state
- Examples: `src/core/app_context.h` (definition), passed as `appctx::AppContext& ctx` throughout
- Pattern: Dependency injection via mutable reference parameter — no service locator or DI container

**`eh::Result<T>`:**
- Purpose: Uniform error handling without exceptions for recoverable errors
- Examples: `src/core/error_handle.h`
- Pattern: `std::expected<T, std::string>` aliased; `eh::makeError(format, args...)` for error creation; checked via `if (!result)` or `result.has_value()`

**`jobstate::Store`:**
- Purpose: Resumable job state with atomic flush to JSON, merge logic for restarts
- Examples: `src/core/job_state.h`, `src/core/job_state.cpp` (654 lines), `src/core/job_state_store.cpp` (250 lines)
- Pattern: Owns `Snapshot` struct serializable to/from JSON; `mergeTasks()` combines planned tasks with persisted state; `flush()` writes atomically via temp file + rename

**`taskexec::TaskPlan` / `runTasks()`:**
- Purpose: Generic parallel task execution with progress bar integration
- Examples: `src/core/task_executor.h`, `src/core/task_executor.cpp` (80 lines)
- Pattern: Declarative plan with `vector<TaskSpec>` and `maxConcurrency`; each `TaskSpec` has an `id`, `label`, and `run` function returning `Result<void>`. Tasks execute on a thread pool with progress bar per slot.

**`pack::PackPlan` / `runPackPlan()`:**
- Purpose: Declarative ZIP archive creation with grouping, naming, and progress callbacks
- Examples: `src/pack/pack_service.h`, `src/pack/pack_service.cpp` (319 lines)
- Pattern: Plan contains groups of `PackFileEntry`, output dir, naming functions, and callbacks for lifecycle events; `runPackPlan()` converts to `TaskPlan` and delegates to `taskexec`

**`EncodeConfig`:**
- Purpose: Structured representation of `ffmpeg` encode configuration with validation and command-line generation
- Examples: `src/video/encode_config.h` (110 lines, header-only)
- Pattern: Value type with `validate()` returning `Result<void>` and `buildCMD()` returning the `ffmpeg` CLI string

**`progress::ProgressContext`:**
- Purpose: Thread-safe multi-bar terminal progress display
- Examples: `src/core/progress.h`, `src/core/progress.cpp` (230 lines)
- Pattern: Wraps `indicators::DynamicProgress` with mutex; bars colored by `Tone` enum; `CursorGuard` RAII class hides cursor during progress

## Entry Points

**Main entry point:**
- Location: `src/main.cpp`
- Triggers: Process launch
- Responsibilities: Crash handler installation → exception-safe delegate to `appentry::run()`

**`appentry::run()`:**
- Location: `src/app/app_entry.cpp`
- Triggers: Called from `main()`
- Responsibilities: Full startup lifecycle — stop signal, CLI parse, config build, toolchain resolve, pipeline execution, error reporting

**`pipeline::run()`:**
- Location: `src/app/pipeline.cpp`
- Triggers: Called from `app_entry`
- Responsibilities: Optional job state init → dispatch to `runVideo()`, `runPicture()`, or `runPackOnly()`

**E2E test tool:**
- Location: `tests/e2e/fake_media_tool.cpp`
- Triggers: Built as separate `encro_e2e_tool` target
- Responsibilities: Generates fake media files for E2E tests

## Architectural Constraints

- **Threading:** Uses `thread-pool` library for parallel task execution. `std::mutex` protects shared mutable state in `ProgressContext`, `jobstate::Store`, and `EncodingState`. Immutable `immer` data structures used for concurrent-read scenarios (video info cache, encode results).
- **Global state:** No persistent global mutable state. `spdlog` uses a static async thread pool initialized once via `std::call_once`. `stop_signal` uses a file-scope atomic flag. `terminal` uses a file-scope `ColorMode` state.
- **Circular imports:** Not detected. All includes follow the layered dependency direction (lower layers do not include higher layers).
- **Platform:** Windows-first with conditional compilation (`_WIN32`/`_WIN64`) for Windows-specific APIs (`dbghelp` for stack traces, `_dupenv_s`, `GetConsoleScreenBufferInfo`). Non-Windows paths exist for `HOME`-based log directory and `dl` linking.
- **Language:** C++26 with `clang-cl` toolchain. Uses `std::expected`, `std::format`, `std::span`, `std::ranges`.

## Anti-Patterns

### God struct: `AppContext`

**What happens:** `appctx::AppContext` bundles config (20+ fields), toolchain paths, and runtime state (job state, video cache) into one struct passed to nearly every function.
**Why it's wrong:** Functions receive and depend on the entire context even when they only need a subset. For example, `readAllVids` takes all three sub-structs (`config`, `toolchain`, `runtime`) but most video info functions only need `toolchain`.
**Do this instead:** Split into focused parameter groups. `getVidInfo` already does this — it takes only `ToolchainPaths` and `videoPath`. Apply this pattern more broadly.

### Mixed abstraction levels in `video_process.cpp`

**What happens:** `video_process.cpp` (544 lines, ~17KB) mixes high-level workflow orchestration, low-level `ffmpeg` command construction, file I/O, progress formatting, and pack integration in a single file.
**Why it's wrong:** The file has too many responsibilities — scanning, planning, encoding, packing, summary printing. Changes to any sub-concern risk affecting others.
**Do this instead:** Extract `packEncodedVideos` to a separate pack-bridge module. Move `printEncodingSummary` to a reporting module. Keep `video_process.cpp` focused on workflow orchestration only.

### `#include` of everything via `app_context.h`

**What happens:** `app_context.h` includes `<boost/json.hpp>`, `<immer/atom.hpp>`, `<immer/map.hpp>`, and many std headers. Every file that includes `app_context.h` (which is most files) pays the compile-time cost.
**Why it's wrong:** Long compile times; unnecessary recompilation when any dependency changes.
**Do this instead:** Forward-declare `immer::map` and `boost::json::value` in `app_context.h`, move the full includes to `.cpp` files. The `RuntimeContext` nested types that use `immer::map` and `immer::atom` templates internally could use PIMPL.

## Error Handling

**Strategy:** Result-type based. Recoverable errors use `eh::Result<T>` (`std::expected<T, std::string>`). Unrecoverable errors (crashes, unhandled exceptions) are caught by `crash_runtime` handlers.

**Patterns:**
- Functions return `eh::Result<T>`. Callers check with `if (!result)` and propagate or handle.
- `eh::makeError(format_string, args...)` creates formatted error `std::string` inside `std::unexpected`.
- Top-level `appentry::run()` converts `Result` errors to terminal output and exit codes.
- `spdlog::error()` for logging failures in verbose mode; `terminal::println(Error, ...)` for user-facing errors.
- Exceptions from `std::format`/`std::filesystem` are caught by `main()`'s catch-all and routed to `crash::reportCaughtException`.

## Cross-Cutting Concerns

**Logging:** `spdlog` async logger created in `prelude::initStartup()`. Enabled only when `--verbose` flag is set. Logs to `%LOCALAPPDATA%/encro/logs/encro.verbose.log` (Windows) or `~/.local/state/encro/logs/` (Unix). Optional `--verbose-echo` mirrors logs to stdout.

**Validation:** `EncodeConfig::validate()` returns `eh::Result<void>`. `cmd::buildConfig()` validates CLI args during translation. `AppConfig` uses `std::optional` for optional fields with defaults applied in `config_builder.cpp`.

**Authentication:** Not applicable — this is a local CLI tool with no remote services.

---

*Architecture analysis: 2026-04-28*
