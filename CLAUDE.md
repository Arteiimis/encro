<!-- GSD:project-start source:PROJECT.md -->
## Project

**Encro — 日志系统优化**

对 encro CLI 媒体编码工具的日志系统进行全面增强。让每一条日志都携带来源位置、模块标签、阶段耗时，出错时提供完整的操作链路和环境快照。日志文件按每次运行独立存储，保留最近 10 次。目标是让 debug 时看一眼日志就能定位问题，不需要复现。

**Core Value:** 每条日志都能回答三个问题：从哪来的、在干什么、花了多久。

### Constraints

- **Tech stack**: spdlog 生态内（不引入其他日志库），C++26，xmake，clang-cl/lld-link
- **性能**: 日志必须保持 async，不能阻塞主流程或编码 pipeline
- **兼容性**: 不改动业务逻辑文件的核心行为，日志调用点可微调但语义不变
- **平台**: 主要 Windows x64，POSIX 路径保留兼容
<!-- GSD:project-end -->

<!-- GSD:stack-start source:codebase/STACK.md -->
## Technology Stack

## Languages
- C++26 (latest working draft) - Entire codebase. Configured via `set_languages("c++26")` in `xmake.lua`. Key C++26 features in use: `std::expected`, `std::format`, `std::span`, `std::stop_token`.
- Lua 5.x - xmake build system configuration (`xmake.lua`, plugin scripts in `plugins/`)
## Runtime
- Native executable (no managed runtime). Compiled via Clang/LLVM toolchain.
- Compiler: `clang-cl` (LLVM C/C++ compiler in MSVC compatibility mode)
- Linker: `lld-link` (LLVM linker)
- xmake built-in package manager (xrepo). Lockfile: not present (packages resolved dynamically from xmake-repo).
## Frameworks
- None. This is a standalone native CLI application with no application framework.
- CLI11 - Command-line argument parsing. Migration from Boost.ProgramOptions completed in v1.6. See `src/cmd/cmd.h` for the parse result struct and `src/cmd/cmd.cpp` for CLI11 setup.
- Catch2 v3 - Test framework. Custom runner in `tests/test_main.cpp`. Included via `catch2/catch_all.hpp`.
- xmake - Build system (2.9.x compatible). Not CMake. All build configuration in `xmake.lua` at project root.
- clang-format - Code formatting. Config file at external path `D:/clangformat/.clang-format` (not in repo).
- llvm-profdata + llvm-cov - Code coverage toolchain (LLVM source-based coverage).
## Key Dependencies
- `boost[all]` - Boost (all modules). Key modules used: `json` (JSON parsing for video info cache, state persistence), `process::v1` (subprocess execution for FFmpeg/FFprobe), `stacktrace` (crash diagnostic), `uuid` (job ID generation), `filesystem` (cross-platform path handling), `program_options` (legacy CLI parsing, being phased out). See `src/core/app_context.h`, `src/utils/utils.cpp`, `src/infra/stacktrace.cpp`.
- `fmt` 10.x+ - String formatting library. Used throughout for `std::format_string` and log messages.
- `spdlog` - Asynchronous logging (file + optional stdout echo). Configured in `src/app/prelude.cpp` with async thread pool (queue size 8192, 1 thread). Log file at `%LOCALAPPDATA%/encro/logs/encro.verbose.log` on Windows.
- `cli11` - CLI argument parsing. Replaces Boost.ProgramOptions. See `src/cmd/cmd.h`.
- `indicators` - Terminal progress bars and spinners. Used in `src/core/progress.h` via `indicators::DynamicProgress` and `indicators::ProgressBar`.
- `libzippp` (+ `libzip`) - ZIP archive creation for output packaging. Used in `src/pack/packer.cpp`. Windows requires `toolchains=clang-cl` config for libzippp and `toolchains=clang` for its libzip dependency.
- `immer` - Persistent immutable data structures (`immer::atom<immer::map>`). Used for thread-safe video info cache in `src/core/app_context.h`.
- `catch2` - Test framework. Used in all test targets (`tests`, `e2e_tests`).
- `thread-pool` - Parallel task execution. Used in `src/core/parallel.h` and `src/core/task_executor.cpp` for parallel video encoding and file packing.
## Configuration
- No `.env` files present. All configuration via CLI arguments (CLI11-parsed).
- External tool discovery: FFmpeg and FFprobe discovered via PATH or `--ffmpeg-path` argument. See `src/utils/utils.cpp` (`findFFmpeg`, `findFFprobe`) and `src/infra/toolchain.cpp`.
- Log directory resolved from `LOCALAPPDATA` (Windows), XDG `STATE_HOME` (Linux), or `TEMP` fallback. See `src/app/prelude.cpp` (`resolveCommonLogDir`).
- `xmake.lua` - Primary build configuration (root). Defines 4 build modes, 3 targets, packaging rules.
- `.vscode/settings.json` - VS Code editor settings (format-on-save enabled).
- clang-format config at `D:/clangformat/.clang-format` - External path, referenced by both `xmake format` and `.githooks/pre-commit`.
## Platform Requirements
- Clang/LLVM toolchain (clang-cl, lld-link on Windows; or clang++, lld on Linux)
- xmake 2.9+
- clang-format (for code formatting; config at `D:/clangformat/.clang-format`)
- llvm-profdata + llvm-cov (for coverage reports)
- Git with Bash shell (pre-commit hook is a bash script)
- Primary target: Windows x64 (with `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `_MSVC_STL_HARDENING=1` defines)
- Cross-platform: POSIX code paths exist in `src/utils/utils.cpp` for Linux/macOS
- Packaging: NSIS installer, source archives, zip/tar.xz via xmake's built-in xpack. See `xmake.lua` lines 106-114.
- User must install FFmpeg and FFprobe separately (not bundled). See external toolchain discovery in `src/infra/toolchain.cpp`.
## Build Modes
| Mode | Flags | Purpose |
|------|-------|---------|
| `debug` | ASan enabled (`build.sanitizer.address`), MD runtime | Development/debugging with address sanitizer |
| `release` | LTO enabled (`build.optimization.lto`) | Production builds |
| `releasedbg` | Optimized + debug info | Release-like debugging |
| `coverage` | `-fprofile-instr-generate -fcoverage-mapping`, LTO disabled | Source-based code coverage |
<!-- GSD:stack-end -->

<!-- GSD:conventions-start source:CONVENTIONS.md -->
## Conventions

## Language and Standard
## Naming Patterns
- `snake_case` for all source, header, and test files.
- Examples: `video_info.h`, `job_state_tests.cpp`, `config_builder.cpp`
- Header files use `.h`, implementation files use `.cpp`.
- `PascalCase`
- Examples: `ConfigSnapshot`, `PackService`, `TaskRecord`, `MessageKind`, `OutputLayout`
- `camelCase`
- Examples: `mergeTasks()`, `markRunning()`, `findFFmpeg()`, `runIndexedTasks()`, `isStopRequested()`
- `camelCase` + trailing underscore `_`
- Examples: `stateFilePath_`, `mtx_`, `lastFlushAtMs_`, `snapshot_`, `outputFormat`
- `k` + `PascalCase`
- Examples: `kEncodeVideoKind`, `kFlushIntervalMs`, `kCrashChildArg`, `kCanceledExitCode`
- Lowercase, no separators (no underscores or hyphens).
- Examples: `jobstate`, `appentry`, `videobatch`, `taskexec`, `stopsignal`
- Namespace braces do not cause indentation (body not indented).
- Forward declarations of classes use namespace blocks:
- Common shorthand alias: `namespace fs = std::filesystem;` (declared within each namespace that needs it, at namespace scope).
- `Ty` for a single type parameter.
- `Tys` for a parameter pack.
- Short namespace aliases for frequently used error handling:
## Code Style
- **Tool:** `clang-format`
- **Config location (external):** `D:/clangformat/.clang-format` (not in repo)
- **Apply:** `xmake format`
- **Check-only:** `xmake format -k check`
- **Pre-commit hook:** `.githooks/pre-commit` runs `clang-format` on staged C/C++ files against the external config file.
- Hook setup: `git config core.hooksPath .githooks`
- **East const:** `std::string const&`, `fs::path const&` (const always on the right).
- **Trailing return type on ALL functions, including `main`:** `auto fn(params) -> ReturnType`
- `#pragma once` ONLY. Never use `#ifndef`/`#define` guards.
- Always braces for control flow (enforced by clang-format configuration).
- Used extensively for constructing aggregates:
- Used for file-local/internal linkage (instead of `static`):
- In tests, anonymous namespaces hold helper functions that are not shared via `test_utils.h`.
- In implementation files, anonymous namespaces hold private helper functions and constants not exposed in headers.
## Import Organization
- Include paths are relative to `src/` or `tests/` directory with forward slashes.
- Example: `#include "core/error_handle.h"` (not `../core/error_handle.h`)
#include "core/task_executor.h"     // own header
#include "core/parallel.h"           // project: core
#include "infra/stop_signal.h"       // project: infra
#include <algorithm>                  // stdlib
#include <atomic>
#include <exception>
#include <format>
#include <optional>
## Error Handling
- All operational failures use `eh::Result<T>` return types.
- Exceptions are reserved for catastrophic/unrecoverable errors only (caught in `main.cpp`).
## Logging
- Log pattern set via `spdlog::set_pattern(kLogPattern)`.
- Two sinks by default: file sink (`%LOCALAPPDATA%/encro/logs/`) + stdout color sink.
- Verbose echo mode: writes all log levels; non-verbose: suppresses trace/debug.
- `spdlog::error("{}", message)` for error-level messages.
- `spdlog::warn("...")` for warnings.
- `crash::reportCaughtException()` and `crash::reportUnknownException()` for crash reporting (use spdlog internally).
- In tests, `spdlog` can be captured via `ScopedDefaultLogger` (RAII fixture that temporarily replaces the default logger with a file-sink logger for verification).
- The `terminal` namespace (`src/infra/terminal.h`) provides styled text output through `fmt`-based template functions: `terminal::println()`, `terminal::println()`, `terminal::eprint()`, `terminal::eprintln()`.
- These use `MessageKind` enum for semantic roles: `Error`, `Warning`, `Success`, `Info`, `Hint`, `Prompt`, `Heading`, `Usage`, `OptionGroup`, `OptionName`, etc.
## Comments
- Module/design-level comments at the top of specific files (e.g., compile-only test files explain the intent: "If this file compiles, the public API boundary is clean.")
- Section separators: `// ---` or inline separator comments like `// ── Phase 20 additions ──`
- Inline comments for non-obvious logic or platform-specific code paths.
- No `TODO`, `FIXME`, `HACK`, or `XXX` comments were found in the codebase.
## Function Design
- `std::string_view` for read-only string parameters.
- `fs::path const&` for filesystem paths.
- `std::span<T const>` for array/vector views.
- `appctx::AppContext&` (mutable reference) for the single shared context struct that flows through the call chain.
- `std::optional<T>` for nullable parameters (never raw pointers for optionals).
- `eh::Result<T>` for failable operations.
- `std::optional<T>` for queries that may not find a result.
- Plain values for infallible operations.
- Trailing return type syntax on ALL functions.
## Module Design
- Public API of each module exposed through the primary header (e.g., `pack/pack.h` is the single public header for the `pack` module).
- Implementation details in `*_internal.h` or `*_types.h` headers, consumed within the module and by tests.
- Clean header boundaries verified by compile-only tests (e.g., `tests/pack_api_standalone_compile_test.cpp` asserts `pack.h` compiles without including `pack_service.h`).
- Defined inline in headers (e.g., `src/infra/terminal.h` template printing functions).
- `Packer` class uses many `static` private methods for grouping logic, avoiding unnecessary state.
## Code Structure
- `appctx::AppContext` (`src/core/app_context.h`) is the single mutable context struct passed by mutable reference through the entire call chain.
- Free functions preferred over classes where state is not needed.
- `static` class methods used for stateless utility grouping.
- Immutable data structures (`immer::atom`, `immer::map`) for thread-safe sharing in `RuntimeContext`.
## Platform Conventions
- Defines: `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `_MSVC_STL_HARDENING=1`
- Cross-platform support via `#if defined(_WIN32)` / `#else` blocks in `src/utils/utils.cpp` and other files.
- Preprocessor guards use `_WIN32` / `_WIN64` for Windows, plain `#else` for POSIX.
## Anti-Patterns to Avoid
### Using `#ifndef` header guards
### West const
### Non-trailing return type
### Throwing exceptions for operational errors
### Including internal module headers from outside
### Skipping error checks on `eh::Result`
### Not using East const for template parameters
<!-- GSD:conventions-end -->

<!-- GSD:architecture-start source:ARCHITECTURE.md -->
## Architecture

## System Overview
```text
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
### Picture Processing Flow
### Pack-Only Flow
### Resume/Restore Flow
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
### Video encoding progress monitor busy-polls
## Error Handling
- All domain functions return `eh::Result<T>` or `eh::Result<void>`. The caller checks with `if (!result)` and propagates with `eh::makeError("context: {}", result.error())`.
- `EncodeConfig::validate()` returns `eh::Result<void>` — called before command construction.
- `crash::installHandlers()` sets SEH (Windows structured exception handler) and `std::set_terminate` for unhandled C++ exceptions. Reports via `spdlog` and prints stacktrace.
- `stopsignal::installHandler()` catches SIGINT/Ctrl+C and sets an atomic flag. Long-running operations check `stopsignal::isStopRequested()` periodically and return `kCanceledExitCode` (130).
## Cross-Cutting Concerns
<!-- GSD:architecture-end -->

<!-- GSD:skills-start source:skills/ -->
## Project Skills

No project skills found. Add skills to any of: `.claude/skills/`, `.agents/skills/`, `.cursor/skills/`, `.github/skills/`, or `.codex/skills/` with a `SKILL.md` index file.
<!-- GSD:skills-end -->

<!-- GSD:workflow-start source:GSD defaults -->
## GSD Workflow Enforcement

Before using Edit, Write, or other file-changing tools, start work through a GSD command so planning artifacts and execution context stay in sync.

Use these entry points:
- `/gsd-quick` for small fixes, doc updates, and ad-hoc tasks
- `/gsd-debug` for investigation and bug fixing
- `/gsd-execute-phase` for planned phase work

Do not make direct repo edits outside a GSD workflow unless the user explicitly asks to bypass it.
<!-- GSD:workflow-end -->



<!-- GSD:profile-start -->
## Developer Profile

> Profile not yet configured. Run `/gsd-profile-user` to generate your developer profile.
> This section is managed by `generate-claude-profile` -- do not edit manually.
<!-- GSD:profile-end -->
