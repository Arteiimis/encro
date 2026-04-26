---
focus: arch
last_mapped_commit: 919b0cea076d2821618c3febf54f72285880cd4c
mapped_at: 2026-04-26
---

<!-- refreshed: 2026-04-26 -->
# Architecture

**Analysis Date:** 2026-04-26

## System Overview

```text
┌──────────────────────────────────────────────────────────────────────┐
│                        Application Layer                             │
│  `src/app/`                                                          │
│  ┌──────────────┬──────────────┬──────────────────────────────────┐  │
│  │  app_entry   │   prelude    │           pipeline               │  │
│  │  (entry pt)  │  (startup)   │        (orchestration)           │  │
│  └──────┬───────┴──────┬───────┴────────────┬─────────────────────┘  │
└─────────┼──────────────┼────────────────────┼────────────────────────┘
          │              │                    │
          ▼              ▼                    ▼
┌──────────────────────────────────────────────────────────────────────┐
│                         Domain Modules                                │
│  ┌─────────────┐  ┌──────────────┐  ┌──────────────────────────────┐ │
│  │   cmd/       │  │   video/     │  │   picture/                   │ │
│  │ CLI parsing  │  │ encode,      │  │   scan, compress,            │ │
│  │ config build │  │ batch, info, │  │   pack                       │ │
│  └──────┬───────┘  │ plan, parse  │  └──────────────┬───────────────┘ │
│         │          └──────┬───────┘                 │                 │
│         │                 │                         │                 │
│         │          ┌──────┴─────────────────────────┴──────┐          │
│         │          │              pack/                    │          │
│         │          │   zip creation, file grouping,        │          │
│         │          │   pack planning                       │          │
│         │          └──────┬─────────────────────────┬──────┘          │
└─────────┼─────────────────┼─────────────────────────┼─────────────────┘
          │                 │                         │
          ▼                 ▼                         ▼
┌──────────────────────────────────────────────────────────────────────┐
│                          Core Layer                                   │
│  `src/core/`                                                          │
│  ┌──────────┬──────────┬──────────┬──────────┬───────────────────┐   │
│  │ app_ctx  │ error_   │ task_    │ progress │ job_state          │   │
│  │ (config  │ handle   │ executor │ (bars)   │ (persistent        │   │
│  │  +state) │ (Result) │ (plan)   │          │  resume state)     │   │
│  ├──────────┼──────────┼──────────┼──────────┼───────────────────┤   │
│  │ parallel │ media_   │ collision│ display_ │ archive_plan       │   │
│  │ (pool)   │ scanner  │ _naming  │ text     │ path_roots         │   │
│  └──────────┴──────────┴──────────┴──────────┴───────────────────┘   │
└──────────────────────────────────────────────────────────────────────┘
          │
          ▼
┌──────────────────────────────────────────────────────────────────────┐
│                       Infrastructure Layer                            │
│  `src/infra/`                                                         │
│  ┌──────────┬──────────┬──────────┬──────────┬───────────────────┐   │
│  │ crash_   │ stacktr  │ terminal │ stop_    │ toolchain          │   │
│  │ runtime  │ ace      │ (I/O+f  │ signal   │ (ffmpeg/ffprobe    │   │
│  │ (SEH)    │ (symbols)│  mt)     │ (Ctrl+C) │  resolution)       │   │
│  └──────────┴──────────┴──────────┴──────────┴───────────────────┘   │
│  ┌──────────┐                                                         │
│  │ console_ │                                                         │
│  │ width    │                                                         │
│  └──────────┘                                                         │
└──────────────────────────────────────────────────────────────────────┘
          │
          ▼
┌──────────────────────────────────────────────────────────────────────┐
│                         Utilities Layer                               │
│  `src/utils/`                                                         │
│  ┌────────────────────────────────────────────────────────────────┐  │
│  │ exec2 (subprocess), ffmpeg/ffprobe finder, user input, getUUID │  │
│  └────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────┘
          │
          ▼
┌──────────────────────────────────────────────────────────────────────┐
│                       External Dependencies                           │
│  ┌──────────┬──────────┬──────────┬──────────┬───────────────────┐   │
│  │ FFmpeg/  │ boost    │ libzippp │ spdlog   │ immer (persistent │   │
│  │ ffprobe  │ (json,   │ (zip)    │ (logging)│  data structures) │   │
│  │ (ext CLI)│  prog_op │          │          │                   │   │
│  └──────────┴──────────┴──────────┴──────────┴───────────────────┘   │
└──────────────────────────────────────────────────────────────────────┘
```

## Component Responsibilities

| Component | Responsibility | File |
|-----------|----------------|------|
| `main` | Crash handler installation, top-level exception guard | `src/main.cpp` |
| `appentry` | Application entry point: orchestrate startup → pipeline | `src/app/app_entry.h`, `src/app/app_entry.cpp` |
| `prelude` | CLI parse + log setup + config summary, builds `StartupContext` | `src/app/prelude.h`, `src/app/prelude.cpp` |
| `pipeline` | Dispatch based on `processType` (video/picture); job state init | `src/app/pipeline.h`, `src/app/pipeline.cpp` |
| `cmd` | CLI argument parsing via boost::program_options | `src/cmd/cmd.h`, `src/cmd/cmd.cpp` |
| `config_builder` | Build `AppConfig` from parsed CLI variables map | `src/cmd/config_builder.h`, `src/cmd/config_builder.cpp` |
| `video_process` | Video workflow entry: single-file, multi-file, directory encoding | `src/video/video_process.h`, `src/video/video_process.cpp` |
| `video_batch_execution` | Parallel batch encoding with progress bars and monitoring | `src/video/video_batch_execution.h`, `src/video/video_batch_execution.cpp` |
| `video_encode_runner` | Single video encode: config build, ffmpeg subprocess, WebP adaptive quality | `src/video/video_encode_runner.h`, `src/video/video_encode_runner.cpp` |
| `video_info` | ffprobe video info, HEVC detection, video scanning (extensions + actual codec) | `src/video/video_info.h`, `src/video/video_info.cpp` |
| `video_output_planning` | Plan output file paths for all videos (flat / keep layout) | `src/video/video_output_planning.h`, `src/video/video_output_planning.cpp` |
| `video_progress_parser` | Parse ffmpeg `-progress` output files (frame count, status) | `src/video/video_progress_parser.h`, `src/video/video_progress_parser.cpp` |
| `picture_process` | Picture scan + pack workflow; entry name planning | `src/picture/picture_process.h`, `src/picture/picture_process.cpp` |
| `picture_compress` | Image compression via ffmpeg (batch parallel) | `src/picture/picture_compress.h`, `src/picture/picture_compress.cpp` |
| `packer` | Zip file creation, file grouping, directory pack workflows | `src/pack/packer.h`, `src/pack/packer.cpp` |
| `pack_service` | Pack plan execution, ordinal range naming | `src/pack/pack_service.h`, `src/pack/pack_service.cpp` |
| `app_context` | Central config + runtime state struct, toolchain paths, encoding state per file | `src/core/app_context.h` |
| `error_handle` | `eh::Result<T>` = `std::expected<T, std::string>`, `eh::makeError()` | `src/core/error_handle.h` |
| `task_executor` | Generic task plan scheduling with worker pool, stop signal integration | `src/core/task_executor.h`, `src/core/task_executor.cpp` |
| `parallel` | Thread pool abstraction using BS::thread_pool | `src/core/parallel.h`, `src/core/parallel.cpp` |
| `progress` | Terminal progress bars (indicators library), cursor guard, tone/color management | `src/core/progress.h`, `src/core/progress.cpp` |
| `job_state` | Persistent JSON job state: task records, resume/restart, cancel support | `src/core/job_state.h`, `src/core/job_state.cpp`, `src/core/job_state_store.cpp`, `src/core/job_state_detail.h` |
| `media_scanner` | File system scanner filtered by extension list | `src/core/media_scanner.h`, `src/core/media_scanner.cpp` |
| `collision_naming` | FNV-1a hashing, sanitized labels, conflict-handled flat output naming | `src/core/collision_naming.h` |
| `display_text` | Unicode-aware text truncation, display width calculation | `src/core/display_text.h` |
| `archive_plan` | Resumable pack execution planning from job state | `src/core/archive_plan.h`, `src/core/archive_plan.cpp` |
| `path_roots` | Input path normalization, common ancestor resolution | `src/core/path_roots.h` |
| `crash_runtime` | SEH/terminate handlers, stack trace on crash, exception reporting | `src/infra/crash_runtime.h`, `src/infra/crash_runtime.cpp` |
| `stacktrace` | Windows stack trace capture (dbghelp) and formatting | `src/infra/stacktrace.h`, `src/infra/stacktrace.cpp` |
| `terminal` | Colored terminal output (fmt), message kinds, stream dispatch | `src/infra/terminal.h`, `src/infra/terminal.cpp` |
| `stop_signal` | SIGINT/Ctrl+C handler, atomic stop flag, cancel exit code | `src/infra/stop_signal.h`, `src/infra/stop_signal.cpp` |
| `toolchain` | Locate ffmpeg/ffprobe from custom dir, PATH, or env | `src/infra/toolchain.h`, `src/infra/toolchain.cpp` |
| `console_width` | Terminal column width detection | `src/infra/console_width.h`, `src/infra/console_width.cpp` |
| `utils` | Subprocess execution (`exec2`), ffmpeg/ffprobe finder, user prompt, UUID gen | `src/utils/utils.h`, `src/utils/utils.cpp` |

## Pattern Overview

**Overall:** Layered architecture with immutable state sharing

**Key Characteristics:**
- **Single binary** — everything compiles into one `encro.exe`; tests are separate binaries
- **`std::expected`-based error handling** — `eh::Result<T>` (aliased to `std::expected<T, std::string>`) used pervasively; no exceptions across module boundaries (except in top-level crash handlers)
- **Immutable-shared snapshots** — `immer::atom<T>` is used for shared concurrent state (video info cache, active encoding slots, encode results), enabling lock-free reads with atomic updates
- **Task-based parallelism** — `taskexec::TaskPlan`/`TaskSpec` abstract over `parallel::runIndexedTasks` backed by `BS::thread_pool`
- **Job state persistence** — `jobstate::Store` persists a JSON snapshot of all tasks (encode/archive) to disk, enabling resume/restart across runs
- **Header-only utilities where possible** — collision naming, display text, path roots, error handle are header-only (`inline` functions); heavier logic is in `.cpp` files

## Layers

**app/ (Application):**
- Purpose: Entry point, startup coordination, pipeline dispatch
- Location: `src/app/`
- Contains: `main.cpp` entry, `app_entry` (wiring), `prelude` (CLI+logging bootstrap), `pipeline` (process-type dispatch)
- Depends on: All domain modules (`cmd`, `video`, `picture`, `pack`) and infra (`crash_runtime`, `terminal`, `stop_signal`, `toolchain`)
- Used by: `main.cpp` only

**cmd/ (CLI):**
- Purpose: Parse command-line arguments, build typed `AppConfig`
- Location: `src/cmd/`
- Contains: `cmd.h/.cpp` (parsing), `config_builder.h/.cpp` (config construction)
- Depends on: `core/app_context.h`, `core/error_handle.h`, boost::program_options
- Used by: `app/prelude.cpp`

**video/ (Video Processing):**
- Purpose: Video scanning, encoding, output planning, progress monitoring
- Location: `src/video/`
- Contains: 7 modules — process, batch_execution, encode_runner, info, output_planning, progress_parser, workflow_utils
- Depends on: `core/` (app_context, task_executor, progress, job_state, media_scanner, display_text, collision_naming), `infra/` (terminal, stop_signal), `utils/`
- Used by: `app/pipeline.cpp`

**picture/ (Picture Processing):**
- Purpose: Picture scanning, optional compression, packing
- Location: `src/picture/`
- Contains: `picture_process`, `picture_compress`
- Depends on: `core/` (media_scanner, collision_naming), `pack/`, `infra/terminal`
- Used by: `app/pipeline.cpp`

**pack/ (Archiving):**
- Purpose: Zip archive creation, file grouping by size, directory packing
- Location: `src/pack/`
- Contains: `packer.h/.cpp`, `pack_service.h/.cpp`
- Depends on: `core/` (error_handle, progress, collision_naming, job_state), `infra/terminal`, `libzippp`
- Used by: `video/` (for post-encode packing), `picture/` (for picture packing), `app/pipeline.cpp` (for pack-only mode)

**core/ (Core Abstractions):**
- Purpose: Shared types, error handling, task execution, state persistence
- Location: `src/core/`
- Contains: 11 modules — app_context, error_handle, task_executor, parallel, progress, job_state + store + detail, media_scanner, collision_naming, display_text, archive_plan, path_roots
- Depends on: `infra/` (stop_signal), external libs (boost, immer, indicators, spdlog, thread-pool)
- Used by: Everything above this layer

**infra/ (Infrastructure):**
- Purpose: Platform abstractions, terminal I/O, crash handling, external tool resolution
- Location: `src/infra/`
- Contains: crash_runtime, stacktrace, terminal, stop_signal, toolchain, console_width
- Depends on: Platform APIs (Windows dbghelp, signal), fmt, spdlog
- Used by: All layers

**utils/ (Utilities):**
- Purpose: Subprocess execution, filesystem helpers, user input
- Location: `src/utils/`
- Contains: `utils.h/.cpp` — `exec2()`, `findFFmpeg()`, `findFFprobe()`, `readUserIpt()`, `getUUID()`
- Depends on: boost::program_options (for `getParamStr`)
- Used by: `video/`, `picture/`, `pack/`

## Data Flow

### Primary Request Path (video encoding)

1. **Entry** — `main()` at `src/main.cpp:6` installs crash handlers, delegates to `appentry::run(argc, argv)`
2. **Startup** — `prelude::initStartup()` at `src/app/prelude.cpp:135` parses CLI, configures terminal, sets up spdlog async logging
3. **Early exit check** — `handleParseAndHelp()` at `src/app/app_entry.cpp:93` returns early for `--help` or parse errors
4. **Config build** — `cmd::buildConfig(vm)` at `src/cmd/config_builder.cpp` constructs `AppConfig` from parsed options
5. **Toolchain resolve** — `toolchain::resolve()` at `src/infra/toolchain.cpp` locates ffmpeg/ffprobe
6. **Pipeline dispatch** — `pipeline::run()` at `src/app/pipeline.cpp:84` initializes job state if needed, dispatches to `runVideo()` / `runPicture()`
7. **Video scan** — `readAllVids()` at `src/video/video_info.cpp` scans directory (or files) for video files, filters by actual codec via ffprobe
8. **Output planning** — `planVideoOutputFiles()` at `src/video/video_output_planning.cpp` computes output file paths (flat or keep layout)
9. **Batch execution** — `videobatch::runEncodingTasks()` at `src/video/video_batch_execution.cpp:639` prompts user, sets up progress bars, schedules parallel encode tasks
10. **Per-file encoding** — `encodeVideo()` at `src/video/video_encode_runner.cpp:302` builds ffmpeg command, invokes subprocess, monitors progress file
11. **Post-encode pack** — `packEncodedVideos()` at `src/video/video_process.cpp:358` (if `--pack-output`) groups encoded files into zip archives
12. **Summary** — `printEncodingSummary()` at `src/video/video_process.cpp:439` prints success/failure counts

### Picture Processing Path

1. Pipeline dispatches to `runPicture()` at `src/app/pipeline.cpp:71`
2. `runPicturePackWorkflow()` at `src/picture/picture_process.cpp` scans for images by extension (jpg, jpeg, png, webp, bmp, gif)
3. Optionally compresses images via `compressImageBatch()` (`src/picture/picture_compress.cpp`)
4. Plans zip entry names (flat or keep layout) with collision handling
5. Groups files by size and packs via `packAllFilesInDirectory()` / `runPackPlan()`

### Pack-Only Path

1. `--pack-only` flag triggers `runPackOnly()` at `src/app/pipeline.cpp:42`
2. Delegates to `runDirectoryPackWorkflow()` at `src/pack/packer.cpp`

**State Management:**
- **Immutable shared state**: `immer::atom<map<path, json::value>>` for video info cache (`RuntimeContext::videoInfoCache` in `src/core/app_context.h:92`), `immer::atom<SharedSnapshot>` for active encoding slots in batch execution
- **Persistent job state**: `jobstate::Store` (`src/core/job_state.h:72`) serializes a `Snapshot` to a JSON file, providing resume/restart across process invocations
- **Atomic flags**: `stopsignal::isStopRequested()` for graceful shutdown; `std::atomic<float>` for per-file encoding progress
- **Mutex-guarded mutable state**: `EncodingState::mtx` for per-file concurrent state; `ProgressContext::mtx_` for progress bar updates; `jobstate::Store::mtx_` for snapshot writes

## Key Abstractions

**`eh::Result<T>` (error handling):**
- Purpose: Typed result-or-error without exceptions
- Examples: `src/core/error_handle.h` defines `template<class Ty> using Result = std::expected<Ty, std::string>` and `eh::makeError(fmt, args...)`
- Pattern: All functions that can fail return `eh::Result<T>`; callers check with `if (!res) { ... res.error() ... }`

**`taskexec::TaskPlan` / `TaskSpec` (task scheduling):**
- Purpose: Declarative parallel task execution
- Examples: `src/core/task_executor.h` — `TaskSpec` has `id`, `label`, `run(taskCtx) -> Result<void>`; `TaskPlan` carries list of specs, max concurrency, progress context
- Pattern: Caller builds a `TaskPlan`, calls `runTasks(plan)`, gets `TaskRunResult` with per-task results and cancel status

**`jobstate::Store` (resumable state):**
- Purpose: Persistent job state tracking across process restarts
- Examples: `src/core/job_state.h` — `Store` holds `Snapshot` with `ConfigSnapshot` + `vector<TaskRecord>`; supports merge, mark*, flush operations
- Pattern: On startup, `mergeTasks()` combines planned tasks with stored state; completed tasks are skipped; interrupted tasks are resumed

**`progress::ProgressContext` (progress bars):**
- Purpose: Thread-safe terminal progress bar management
- Examples: `src/core/progress.h` — wraps `indicators::DynamicProgress`, supports adding bars, setting progress/color/tone per bar
- Pattern: Used by `video_batch_execution` for multi-slot encoding progress and overall progress bar

**`encoding::EncodeConfig` (ffmpeg command builder):**
- Purpose: Build validated ffmpeg command strings from structured config
- Examples: `src/video/encode_config.h` — struct with `buildCMD()`, `buildOutputPath()`, `buildOutputFileName()`, `validate()`
- Pattern: Immutable config → validated → command string; CRF-based for video, quality-adaptive for WebP

## Entry Points

**`main()`:**
- Location: `src/main.cpp:6`
- Triggers: Process start (CLI invocation)
- Responsibilities: Install crash handlers, invoke `appentry::run()`, catch and report unhandled exceptions

**`appentry::run()`:**
- Location: `src/app/app_entry.cpp:161`
- Triggers: `main()`
- Responsibilities: Init stop signal handler, parse CLI, build config, resolve toolchain, run pipeline

**`pipeline::run()`:**
- Location: `src/app/pipeline.cpp:84`
- Triggers: `appentry::run()`
- Responsibilities: Initialize job state if applicable, dispatch to video/picture/pack-only workflows

**Testing entry points:**
- `tests/test_main.cpp` — unit/integration test binary (Catch2)
- `tests/e2e/e2e_test_main.cpp` — end-to-end test binary (Catch2, depends on `encro` binary and `encro_e2e_tool`)

## Architectural Constraints

- **Threading:** Thread pool via `BS::thread_pool` (pause/unpause pattern) in `src/core/parallel.cpp`; concurrent encoding slots with per-file `mutex` in `EncodingState`; monitor thread (`std::jthread`) for polling progress files; single async spdlog thread pool
- **Global state:** Module-level singletons: crash handlers (`src/infra/crash_runtime.cpp`), stop signal flag (`src/infra/stop_signal.cpp`), spdlog thread pool (`src/app/prelude.cpp:106-107` via `std::once_flag`), terminal color mode (`src/infra/terminal.cpp`)
- **Circular imports:** None detected — dependencies flow strictly downward: `app` → `cmd`/`video`/`picture`/`pack` → `core` → `infra` → `utils`. Cross-module dependencies exist within same layer (e.g., `video/` modules import each other, `pack/packer.cpp` imports `pack/pack_service.h`)
- **Platform:** Windows primary target (clang-cl toolchain, dbghelp for stack traces, Windows env vars for log directory); Linux/Mac supported via conditional compilation (`#if defined(_WIN32)` blocks)
- **C++ standard:** C++26 (`set_languages("c++26")` in `xmake.lua:6`)

## Anti-Patterns

### Header-Only Domain Logic in Large Headers

**What happens:** `src/core/collision_naming.h` (177 lines) and `src/video/encode_config.h` (110 lines) contain substantial inline logic (FNV-1a hashing, path manipulation, ffmpeg command building) in headers.
**Why it's wrong:** Every translation unit that includes these headers recompiles the inline functions; slower incremental builds; harder to set breakpoints in debuggers.
**Do this instead:** For pure utility functions this is acceptable (pattern used deliberately). For larger logic like `encode_config.h`, consider moving non-trivial method bodies to a `.cpp` file.

### Module-Level Anonymous Namespace for Internal Functions

**What happens:** Heavy use of anonymous namespaces (`namespace { ... }`) in `.cpp` files for internal helpers (e.g., `src/video/video_process.cpp:28-57`, `src/video/video_batch_execution.cpp:33-752`). The largest file `src/video/video_batch_execution.cpp` has 753 lines, nearly all in an anonymous namespace.
**Why it's wrong:** Makes unit testing internal functions impossible; creates large monolithic translation units.
**Do this instead:** Consider extracting significant internal logic to `*_detail` headers (as done with `job_state_detail.h` for the job_state module). Keep the anonymous namespace for small helpers only.

### Direct Subprocess Execution via exec2

**What happens:** `exec2()` in `src/utils/utils.cpp` is a blocking subprocess call. `encodeVideo()` and `encodeWebpWithTargetSize()` call it synchronously.
**Why it's wrong:** While encoding is parallelized at the batch level (multiple `exec2` calls in different threads), each individual encode blocks a thread for the entire encoding duration.
**Do this instead:** The current approach is pragmatic for CPU-bound video encoding (ffmpeg uses its own threading). If I/O-bound operations appear, consider async subprocess management.

## Error Handling

**Strategy:** `std::expected<T, std::string>` with `eh::makeError(format, args...)` helper

**Patterns:**
- All functions that can fail return `eh::Result<void>` or `eh::Result<T>`
- Callers check with `if (!result) { ... result.error() ... }` or early-return propagate
- Top-level exception handlers in `main()` catch unexpected exceptions and call `crash::reportCaughtException()`
- `taskexec::runTasks()` catches per-task exceptions and wraps them in error results — no task can crash the whole batch
- `stopsignal::isStopRequested()` is checked at key points to abort gracefully with exit code 130

## Cross-Cutting Concerns

**Logging:** spdlog async logger configured in `src/app/prelude.cpp:60-129`. Only active with `--verbose` flag. Log file at `%LOCALAPPDATA%/encro/logs/encro.verbose.log`. `--verbose-echo` additionally prints to stdout. Pattern: `[%Y-%m-%dT%H:%M:%S.%e%z] [%^%l%$] %v`

**Validation:** `EncodeConfig::validate()` in `src/video/encode_config.h:23-45` checks input existence, output format validity, CRF/quality ranges. `cmd::buildConfig()` validates CLI inputs. `jobstate::configMatches()` validates config compatibility for resume.

**Authentication:** Not applicable — this is a local CLI tool with no network authentication.

---

*Architecture analysis: 2026-04-26*
