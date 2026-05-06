<!-- refreshed: 2026-05-07 -->
# Architecture

**Analysis Date:** 2026-05-07

## System Overview

```text
┌───────────────────────────────────────────────────────────────────────┐
│                          CLI Entry Layer                              │
│  `src/main.cpp` → `src/app/app_entry.cpp`                            │
│  Parse args, init logging, route to pipeline                         │
├───────────────┬──────────────────────────┬───────────────────────────┤
│  `src/cmd/`   │  `src/app/prelude.cpp`   │  `src/infra/terminal.h`   │
│  cmd::build   │  setupLogging,           │  styled terminal output    │
│  Config()     │  initStartup()           │                            │
└───────┬───────┴────────────┬─────────────┴────────────┬──────────────┘
        │                    │                          │
        ▼                    ▼                          ▼
┌───────────────────────────────────────────────────────────────────────┐
│                     Pipeline Orchestration                            │
│  `src/app/pipeline.cpp` — pipeline::run(appctx::AppContext&)          │
│  Routes: processType → runVideo / runPicture / runPackOnly            │
├─────────────────────┬──────────────────────┬──────────────────────────┤
│   video_process     │   picture_process     │   pack::execute()        │
│   `src/video/`      │   `src/picture/`      │   `src/pack/pack.h`      │
└─────────┬───────────┴──────────┬───────────┴──────────┬───────────────┘
          │                      │                      │
          ▼                      ▼                      ▼
┌───────────────────────────────────────────────────────────────────────┐
│                    Cross-Cutting Core Layer                            │
│  `src/core/` — AppContext, JobState, TaskExecutor, Progress, MediaScan │
├─────────────────────┬──────────────────────┬──────────────────────────┤
│   infra             │   utils              │   External Tools          │
│   `src/infra/`      │   `src/utils/`       │   ffmpeg / ffprobe        │
│   crash, stop,      │   exec2(), readIpt,  │   libzippp (zip)          │
│   terminal, stack   │   findFFmpeg         │   boost, immer, fmt       │
└─────────────────────┴──────────────────────┴──────────────────────────┘
```

## Component Responsibilities

| Component | Responsibility | File |
|-----------|----------------|------|
| `main()` | Install crash handlers, delegate to `appentry::run()` | `src/main.cpp` |
| `appentry::run()` | Startup init, parse args, build config, resolve toolchain, run pipeline | `src/app/app_entry.cpp` |
| `prelude::initStartup()` | Parse CLI, configure terminal, setup spdlog logging | `src/app/prelude.cpp` |
| `pipeline::run()` | Route by `processType` to video/picture/pack-only workflows, manage job state | `src/app/pipeline.cpp` |
| `cmd::buildConfig()` | Convert Boost program_options `variables_map` into `AppConfig` | `src/cmd/config_builder.cpp` |
| `video_process` | Video scanning, encoding workflow orchestration, post-encode packing | `src/video/video_process.cpp` |
| `video_batch_execution` | Multi-threaded encoding with progress bars, slot-based worker pool | `src/video/video_batch_execution.h` |
| `video_encode_runner` | Single video encode: build ffmpeg command, execute, parse progress | `src/video/video_encode_runner.cpp` |
| `picture_process` | Picture scanning, compression, zip entry planning, pack orchestration | `src/picture/picture_process.cpp` |
| `pack::execute()` | Public entry for all packing: build plan, group files, create ZIPs | `src/pack/pack.cpp` |
| `pack::Packer` | Low-level ZIP creation via libzippp, file grouping/splitting | `src/pack/packer.h` |
| `pack::PackService` | Mid-level pack orchestration: runs PackPlan, handles compact/full progress | `src/pack/pack_service.h` |
| `jobstate::Store` | Resumable job state: task tracking, progress persistence, cancel support | `src/core/job_state.h` |
| `taskexec::runTasks()` | Generic parallel task runner with progress bars | `src/core/task_executor.h` |
| `progress::ProgressContext` | Terminal progress bars via `indicators` library | `src/core/progress.h` |
| `media::scanByExtensions()` | Recursive directory scan filtered by file extensions | `src/core/media_scanner.h` |
| `terminal` | Styled terminal output with color, badges, path formatting | `src/infra/terminal.h` |
| `toolchain::resolve()` | Locate ffmpeg/ffprobe on system PATH or custom install dir | `src/infra/toolchain.h` |

## Pattern Overview

**Overall:** Layered architecture with a central pipeline orchestrator

**Key Characteristics:**
- **Namespace-isolated modules** — each `src/` subdirectory has its own C++ namespace (e.g., `pack::`, `jobstate::`, `videobatch::`)
- **Free-function public API** — core features exposed as free functions (`pack::execute()`, `pipeline::run()`), with internal classes (`Packer`, `PackService`) managed internally
- **`eh::Result<T>` error handling** — `std::expected<T, std::string>` via the `eh` namespace alias; no exceptions for expected failures
- **`appctx::AppContext` as shared state** — single struct holding config, toolchain paths, runtime context (video info cache, job state pointer); passed by mutable reference through the call stack
- **Immutable data structures** — `immer::map`, `immer::vector`, `immer::atom` used for thread-safe shared state in progress tracking and video info caching
- **Job state for resume** — `jobstate::Store` persists task records as JSON to disk, enabling interrupted encodes to resume
- **Explicit CLI→Config→Pipeline flow** — Boost.ProgramOptions parses args into `CmdParseResult`, `cmd::buildConfig()` transforms to `AppConfig`, `pipeline::run()` consumes it

## Layers

**CLI / Entry Layer:**
- Purpose: Parse command-line arguments, initialize logging, route to pipeline
- Location: `src/main.cpp`, `src/app/app_entry.cpp`, `src/app/prelude.cpp`, `src/cmd/`
- Contains: `main()`, `appentry::run()`, `prelude::initStartup()`, `cmd::buildConfig()`, `cmd::commandLineInit()`
- Depends on: `infra/terminal`, `infra/toolchain`, `core/app_context`
- Used by: Nothing (top layer)

**Pipeline Orchestration Layer:**
- Purpose: Route processing type, manage job state lifecycle, coordinate video/picture workflows
- Location: `src/app/pipeline.cpp`
- Contains: `pipeline::run()`, inline free functions for `runVideo`, `runPicture`, `runPackOnly`, `ensureJobState`
- Depends on: `core/app_context`, `core/job_state`, `video/video_process`, `picture/picture_process`, `pack/pack`
- Used by: `appentry::run()`

**Domain Processing Layer:**
- Purpose: Video encoding workflow, picture compression/packing workflow
- Location: `src/video/`, `src/picture/`
- Contains: Video scanning, encode planning, batch execution, progress parsing; picture scanning, compression, zip-entry planning
- Depends on: `core/`, `pack/pack`, `infra/`, `utils/utils`
- Used by: `pipeline::run()`

**Packing Layer:**
- Purpose: ZIP archive creation with grouping, naming strategies, progress reporting
- Location: `src/pack/`
- Contains: `pack::execute()` (public), `PackPlan`, `Packer`, `PackService`, internal types (`pack::detail::`)
- Depends on: `core/app_context`, `core/progress`, `core/collision_naming`, `core/job_state`, `infra/terminal`
- Used by: `video/video_process`, `picture/picture_process`, `app/pipeline`

**Core Services Layer:**
- Purpose: Shared abstractions — app context, job state, task execution, progress bars, media scanning, collision naming
- Location: `src/core/`
- Contains: `appctx::AppContext`, `jobstate::Store`, `taskexec::runTasks()`, `progress::ProgressContext`, `media::scanByExtensions()`, `collisionnaming::*`, `parallel::runIndexedTasks()`, `displaytext::*`, `pathroots::*`, `eh::Result<>`
- Depends on: `boost`, `immer`, `indicators`, `spdlog`, `fmt`, filesystem
- Used by: All layers above

**Infrastructure Layer:**
- Purpose: OS-level concerns — crash handling, terminal I/O, stack traces, stop signals, console width
- Location: `src/infra/`
- Contains: `crash::installHandlers()`, `terminal::*`, `stopsignal::*`, `toolchain::resolve()`, `stacktrace::capture*`, `consolewidth::resolveColumns()`
- Depends on: `fmt`, `boost`, platform APIs (Windows `dbghelp`, `_dupenv_s`)
- Used by: All layers

**Utilities Layer:**
- Purpose: External process execution, FFmpeg discovery, UUID generation, user input prompts
- Location: `src/utils/utils.h`, `src/utils/utils.cpp`
- Contains: `exec2()` (run subprocess, capture output), `readUserIpt()`, `findFFmpeg()`, `findFFprobe()`, `getUUID()`
- Depends on: `boost::program_options`
- Used by: Domain, packing, infrastructure layers

## Data Flow

### Primary Request Path (video encoding + packing)

1. **CLI parse** — `main()` → `appentry::run()` → `prelude::initStartup()` calls `commandLineInit()` (`src/cmd/cmd.cpp`)
2. **Config build** — `buildAppConfig()` calls `cmd::buildConfig(vm)` → `appctx::AppConfig` struct (`src/cmd/config_builder.cpp`)
3. **Toolchain resolve** — `ensureToolchainReady()` calls `toolchain::resolve()` to find ffmpeg/ffprobe paths (`src/infra/toolchain.cpp`)
4. **Pipeline route** — `pipeline::run(ctx)` checks `ctx.config.processType` → calls `runVideo(ctx)` (`src/app/pipeline.cpp`)
5. **Video scan** — `handlePathEncoding()` → `scanInputVideos()` → `readAllVids()` → `media::scanByExtensions()` filters by `.mp4`, `.mkv`, etc. (`src/video/video_process.cpp` → `src/core/media_scanner.cpp`)
6. **Output planning** — `planVideoOutputFiles()` builds `path_map<fs::path>` mapping each input to its planned output file (`src/video/video_output_planning.cpp`)
7. **Job state merge** — `prepareEncodeActions()` merges planned tasks with persisted `jobstate::Store`, filters for pending tasks (`src/video/video_process.cpp`)
8. **Batch encode** — `videobatch::runEncodingTasks()` spawns worker threads, each calls `encodeVideo()` → `exec2(ffmpeg_cmd)` (`src/video/video_batch_execution.h` → `src/video/video_encode_runner.cpp`)
9. **Pack outputs** — `maybePackWorkflowOutputs()` → `packEncodedVideos()` → `pack::execute(PackRequest)` groups encoded files, creates ZIPs via `Packer` → `libzippp` (`src/video/video_process.cpp` → `src/pack/pack.cpp`)
10. **Summary** — `printEncodingSummary()` reports success/failure counts (`src/video/video_process.cpp`)

### Picture Packing Path

1. **Pipeline route** — `pipeline::run(ctx)` with `processType == "picture"` → `runPicture(ctx)` (`src/app/pipeline.cpp`)
2. **Workflow** — `runPicturePackWorkflow()` → either `executeCompressPackWorkflow()` (if `compressImages`) or `executeDirectPackWorkflow()` (`src/picture/picture_process.cpp`)
3. **Compress path (if enabled):** `compressImageBatch()` → `exec2(ffmpeg -c:v libwebp)` per image, then pack compressed outputs (`src/picture/picture_compress.cpp`)
4. **Direct path:** Plan zip entry names → build `PackEntryInput` list → `pack::execute()` (`src/picture/picture_process.cpp`)

**State Management:**
- `appctx::AppContext` is the sole mutable context object, passed by reference through all layers
- `jobstate::Store` (when enabled) persists task records to a JSON file, enabling resume after interruption
- `immer::atom<immer::map<...>>` provides lock-free shared state for video info caching (`RuntimeContext::videoInfoCache`) and encoding progress snapshots
- `std::atomic` used for progress counters, stop signals, and `EncodingState::lastProgressAtomic`
- `std::mutex` guards `EncodingState::mtx` for state transitions, `ProgressContext::mtx_` for bar operations, and `jobstate::Store::mtx_` for snapshot persistence

## Key Abstractions

**`appctx::AppContext`:**
- Purpose: Central context object holding all runtime state
- Examples: `src/core/app_context.h` (struct definition), `pipeline::run()` (consumer), `toolchain::resolve()` (populator)
- Pattern: Pass-by-mutable-reference through the call chain; `AppConfig` is a plain struct; `RuntimeContext` holds `immer::atom` caches and a `shared_ptr<jobstate::Store>`

**`eh::Result<T>`:**
- Purpose: `std::expected<T, std::string>` — success value or string error message
- Examples: `src/core/error_handle.h` (alias: `namespace eh = ErrorHandle`), used pervasively in `pack::execute()`, `pipeline::run()`, `videobatch::runEncodingTasks()`
- Pattern: `eh::makeError(fmt_string, args...)` creates `std::unexpected`, checked with `if (!result)` or `result.has_value()`

**`jobstate::Store`:**
- Purpose: Persistable job state — task records, progress, cancellations — enabling encode/pack resume
- Examples: `src/core/job_state.h` (class definition), `src/core/job_state_detail.h` (serialization), `src/video/video_process.cpp:prepareEncodeActions()` (usage)
- Pattern: Owns a `Snapshot` struct serialized as JSON; `mergeTasks()` merges planned tasks with persisted state; individual tasks marked Running/Succeeded/Failed/Interrupted

**`pack::PackRequest` → `pack::PackPlan` → `pack::Packer`:**
- Purpose: Three-layer packing abstraction — `PackRequest` (public input), `PackPlan` (internal grouping), `Packer` (ZIP I/O)
- Examples: `src/pack/pack.h` (PackRequest), `src/pack/pack_plan_internal.h` (PackPlan), `src/pack/packer.h` (Packer)
- Pattern: `execute(PackRequest)` internally builds a `PackPlan` via `buildMediaPackPlan()` or `Packer::buildDirectoryPackPlan()`, then dispatches to `execute(PackPlan, jobState*)`

**`progress::ProgressContext`:**
- Purpose: Terminal progress bars via `indicators::DynamicProgress<indicators::ProgressBar>`
- Examples: `src/core/progress.h` (class definition), `src/video/video_batch_execution.h:EncodingProgressState` (per-slot bars + overall bar)
- Pattern: Thread-safe bar management with `std::mutex`; `addBar()` returns an index; `setProgress(index, float)` updates; `CursorGuard` hides cursor during progress display

**`collisionnaming`:**
- Purpose: Collision-safe flat naming for files from different source directories
- Examples: `src/core/collision_naming.h` — `buildCollisionGroupPrefix()`, `buildConflictHandledFlatName()`, `sanitizeLabel()`, `shortPathHash()`
- Pattern: FNV-1a 32-bit hash of normalized path → hex string embedded in filename prefix; used by both video output planning and picture zip entry naming

## Entry Points

**`main()`:**
- Location: `src/main.cpp`
- Triggers: Process launch with CLI arguments
- Responsibilities: Install crash handlers, call `appentry::run(argc, argv)`, catch unhandled exceptions

**`appentry::run(int argc, char* argv[])`:**
- Location: `src/app/app_entry.cpp`
- Triggers: Called by `main()`
- Responsibilities: Install stop signal handler, init startup (parse args + logging), handle `--help`, build `AppConfig`, resolve toolchain, run pipeline, return exit code

**`pipeline::run(appctx::AppContext& ctx)`:**
- Location: `src/app/pipeline.cpp`
- Triggers: Called by `appentry::run()` after config+toolchain are ready
- Responsibilities: Conditionally enable job state, route by `processType` to `runVideo()` / `runPicture()` / `runPackOnly()`, return `eh::Result<int>`

**`pack::execute(PackRequest const&)`:**
- Location: `src/pack/pack.cpp`
- Triggers: Called by video post-encode packing, picture workflows, and pack-only mode
- Responsibilities: Build `PackPlan` from request (Media or Directory mode), dispatch to resumable or non-resumable execution, return `eh::Result<PackRunResult>`

## Architectural Constraints

- **Threading:** `taskexec::runTasks()` uses `thread-pool` library for parallel task execution. `videobatch::runEncodingTasks()` uses `std::jthread` with `EncodingProgressState` managing slot-based worker slots. `parallel::runIndexedTasks()` provides a simpler indexed parallel for loop. Progress bars updated from worker threads via `std::atomic` counters and `immer::atom` snapshots — no locks on the hot path.
- **Global state:** `src/infra/terminal.cpp` holds module-level color mode state (`terminal::colorMode()`). `src/infra/stop_signal.cpp` holds module-level `std::atomic_bool` stop flag. `src/app/prelude.cpp` holds `std::once_flag` for spdlog thread pool init. `src/infra/crash_runtime.cpp` holds module-level `std::atomic` handler-installed flag.
- **Circular imports:** Not detected. Dependency graph is acyclic: `app` → `video`/`picture`/`pack`/`core` → `infra`/`utils`. `pack` → `core`. No reverse dependencies.
- **Platform:** Windows-primary (`_WIN32` / `_WIN64` guards, `dbghelp` syslink, `clang-cl` toolchain). Non-Windows paths use `dl` syslink for stack unwinding and Unix-style environment (`HOME`, `~/.local/state`).
- **C++ Standard:** C++26 (`set_languages("c++26")` in `xmake.lua`), with Clang-CL compiler on Windows.

## Anti-Patterns

### Tracing Callback via `std::function<>` Capture

**What happens:** `videobatch::EncodingExecutionContext` captures `appctx::AppContext& app` as a reference member. `EncodingProgressState` is created on the stack and referenced by `EncodingExecutionContext`. This works because both outlive the encoding tasks, but it's fragile under refactoring.
**Why it's wrong:** Lifetime coupling between stack-allocated state objects (`EncodingProgressState`) and long-lived encoding threads (via `jthread`) is implicit. Moving either out of the stack frame would cause dangling references.
**Do this instead:** Consider a shared ownership model (e.g., `shared_ptr` to a combined execution context) or clearly document that `EncodingExecutionContext` must be destroyed after all encoding tasks complete.

### Header-Only Abstractions in `core/`

**What happens:** `src/core/collision_naming.h`, `src/core/path_roots.h`, and `src/core/display_text.h` are entirely inline/header-only — all functions defined in headers.
**Why it's wrong:** Increases compile times for all translation units including these headers. Changes to any function body trigger recompilation of all dependent `.cpp` files.
**Do this instead:** Move non-trivial function bodies to `.cpp` files. Keep only simple template/inline functions in headers. This is especially relevant for `collision_naming.h` (177 lines of inline code) and `display_text.h` (86 lines of inline code).

## Error Handling

**Strategy:** `std::expected<T, std::string>` via the `eh::Result<T>` alias. Operational failures return `eh::makeError(...)` with formatted messages. Catastrophic failures (bad_alloc, etc.) propagate as exceptions caught in `main()`.

**Patterns:**
- Free functions return `eh::Result<T>` or `eh::Result<void>`: `pack::execute()`, `pipeline::run()`, `toolchain::resolve()`
- Chain with early returns: `if (!res) { return eh::makeError("context: {}", res.error()); }`
- Validation uses same pattern: `EncodeConfig::validate() -> eh::Result<void>` returns `makeError(...)` on invalid input
- Local error state: `EncodingState::lastError` stores error string for reporting after task completion

## Cross-Cutting Concerns

**Logging:** `spdlog` with async thread pool. Verbose logging (via `--verbose`) writes to `%LOCALAPPDATA%/encro/logs/encro.verbose.log`. Level: `debug` when verbose, `off` otherwise. Flush-on-error. Pattern: `spdlog::error()`, `spdlog::info()`, `spdlog::debug()` throughout the codebase.

**Validation:** `EncodeConfig::validate()` checks input paths exist, CRF range (0–51), WebP quality range (0–100), format enum. `AppConfig` built from CLI options with bounds checking in `cmd::buildConfig()`.

**Authentication:** Not applicable — offline CLI tool, no authentication required.

---

*Architecture analysis: 2026-05-07*
