<!-- refreshed: 2026-05-22 -->
<!-- last_mapped_commit: 45d19cdfd54971db03ee0758ccaad6c61aad4ff1 -->
# Codebase Structure

**Analysis Date:** 2026-05-22

## Directory Layout

```
encro/
├── .cache/                  # clangd index cache (auto-generated, not committed)
├── .claude/                 # Claude agent worktrees (internal tooling)
├── .githooks/               # Git hooks (pre-commit: clang-format)
├── .planning/               # GSD planning artifacts (codebase docs, config)
│   └── codebase/            # Architecture/stack/conventions docs (this directory)
├── .vscode/                 # VS Code workspace settings
├── .xmake/                  # xmake per-platform build cache (not committed)
├── build/                   # Build output directory (xmake artifacts)
├── plugins/                 # xmake custom plugins
│   ├── coverge/             #   Coverage plugin: llvm-profdata + llvm-cov
│   └── format/              #   clang-format plugin: apply/check on src/ + tests/
├── src/                     # All implementation source code
│   ├── main.cpp             #   Process entry point
│   ├── app/                 #   App lifecycle: CLI entry, pipeline, prelude
│   ├── cmd/                 #   CLI argument parsing + config builder
│   ├── core/                #   Domain types, shared state, executors, utilities
│   ├── infra/               #   Platform services: crash, terminal, signals
│   ├── pack/                #   ZIP archive creation subsystem
│   ├── picture/             #   Image compression + packaging
│   ├── utils/               #   Subprocess, FFmpeg discovery, UUID
│   └── video/               #   Video encoding orchestration
├── tests/                   # All test code
│   ├── test_main.cpp        #   Catch2 custom test runner
│   ├── test_utils.h         #   Test fixtures (TempDir, helpers)
│   ├── app/                 #   Tests for app layer (app_entry, pipeline)
│   ├── infra/               #   Tests for infra layer (terminal, toolchain, ...)
│   ├── picture/             #   Tests for picture subsystem
│   ├── video/               #   Tests for video subsystem
│   └── e2e/                 #   End-to-end tests + fake toolchain
├── AGENTS.md                # Build/run instructions + conventions
├── xmake.lua                # Build configuration (targets, deps, modes)
└── .gitignore               # Git ignore rules
```

## Directory Purposes

### `src/` - Implementation Source
- **Purpose:** All production source code. Organized by layer/module.
- **Contains:** `.cpp` and `.h` files. Headers are next to implementations (no separate `include/` directory).
- **Key files:** `src/main.cpp` (entry point), `src/core/app_context.h` (central context struct)

### `src/app/` - Application Layer
- **Purpose:** Top-level app lifecycle: CLI entry, pipeline orchestration, prelude (logging setup).
- **Contains:** `app_entry.cpp/.h`, `pipeline.cpp/.h`, `prelude.cpp/.h`
- **Key files:** `src/app/app_entry.cpp` (orchestrates full run), `src/app/pipeline.cpp` (routes to video/picture/pack-only)

### `src/cmd/` - CLI Layer
- **Purpose:** CLI11-based argument parsing and conversion to validated `AppConfig`.
- **Contains:** `cmd.cpp/.h` (CLI11 setup), `config_builder.cpp/.h` (parse result -> AppConfig)
- **Key files:** `src/cmd/cmd.h` (defines `CmdParseResult` struct)

### `src/core/` - Core Abstractions
- **Purpose:** Domain types, shared mutable state, reusable executors, error handling, media scanning.
- **Contains:** `app_context.h` (central context), `error_handle.h` (Result type), `job_state.cpp/.h` (persistent state), `task_executor.cpp/.h` (parallel tasks), `parallel.cpp/.h` (indexed parallelism), `media_scanner.cpp/.h` (file scanning), `progress.cpp/.h` (progress bars), `collision_naming.h` (conflict-safe naming), `display_text.h` (Unicode display), `path_roots.h` (path utilities), `job_state_detail.h` (internal helpers)
- **Key files:** `src/core/app_context.h` (defines `AppContext`, `AppConfig`, `RuntimeContext`, `EncodingState`), `src/core/error_handle.h` (defines `eh::Result<T>`)

### `src/infra/` - Infrastructure Layer
- **Purpose:** Platform abstraction: crash handling, terminal styling, signal handling, toolchain discovery.
- **Contains:** `crash_runtime.cpp/.h` (SEH + terminate handler), `terminal.cpp/.h` (colored console output), `stop_signal.cpp/.h` (Ctrl+C handler), `toolchain.cpp/.h` (ffmpeg discovery), `stacktrace.cpp/.h` (stacktrace capture), `console_width.cpp/.h` (terminal width), `env.h` (environment variable reader)
- **Key files:** `src/infra/terminal.h` (styled output with `MessageKind` enum), `src/infra/crash_runtime.h` (crash handler installation)

### `src/pack/` - Pack/ZIP Subsystem
- **Purpose:** Create ZIP archives with file grouping, naming strategies, progress, resumable execution.
- **Contains:** `pack.cpp/.h` (public `execute()` API), `packer.cpp/.h` (low-level ZIP operations), `pack_service.cpp/.h` (orchestration with progress), `pack_types.h` (domain types: `PackFileEntry`, `PackEntryInput`), `packer_types.h` (internal types), `pack_internal.h`, `pack_plan_internal.h`
- **Key files:** `src/pack/pack.h` (defines `PackRequest`, `PackMode`, `NamingStrategy`), `src/pack/packer.h` (640+ line implementation)

### `src/picture/` - Picture Processing
- **Purpose:** Image scanning, compression to WebP via ffmpeg, batch processing, pack-to-ZIP.
- **Contains:** `picture_process.cpp/.h` (workflow orchestration), `picture_compress.cpp/.h` (ffmpeg WebP compression)
- **Key files:** `src/picture/picture_process.h` (public API: `readAllPics`, `runPicturePackWorkflow`, `packAllPicsToZip`)

### `src/video/` - Video Processing (largest subsystem)
- **Purpose:** Video encoding orchestration: scanning, ffprobe info, output planning, parallel encoding, progress monitoring.
- **Contains:** `video_process.cpp/.h` (dispatch by input type), `video_batch_execution.cpp/.h` (parallel encoding + progress), `video_encode_runner.cpp/.h` (ffmpeg invocation), `video_info.cpp/.h` (ffprobe wrapper), `video_output_planning.cpp/.h` (output path computation), `video_progress_parser.cpp/.h` (ffmpeg progress parsing), `video_encoding_state.cpp` (progress monitor thread), `encode_config.h` (ffmpeg command builder), `video_workflow_utils.h` (job state helpers)
- **Key files:** `src/video/video_batch_execution.h` (~280 lines of encoding execution context), `src/video/video_process.cpp` (~530 lines of dispatch logic)

### `src/utils/` - Utilities
- **Purpose:** Cross-cutting helpers: subprocess execution, ffmpeg/ffprobe path discovery, UUID generation.
- **Contains:** `utils.cpp/.h`
- **Key files:** `src/utils/utils.h` (defines `exec2()`, `readUserIpt()`, `findFFmpeg()`, `findFFprobe()`, `getUUID()`)

### `tests/` - Test Code
- **Purpose:** Unit, integration, and end-to-end tests. Uses Catch2 v3.
- **Contains:** `test_main.cpp` (custom runner), `test_utils.h` (fixtures: `TempDir`, `ScopedStopSignalReset`, file helpers), module-named test files (`*_tests.cpp`), standalone compile checks (`*_standalone_compile_test.cpp`)
- **Key files:** `tests/test_main.cpp` (Catch2 session config), `tests/test_utils.h` (shared test infrastructure)

### `tests/e2e/` - End-to-End Tests
- **Purpose:** Full integration tests using a fake ffmpeg/ffprobe toolchain.
- **Contains:** `encro_e2e_tests.cpp` (Catch2 test cases), `e2e_test_main.cpp` (separate test runner), `e2e_test_utils.cpp/.h` (helpers), `fake_media_tool.cpp` (build target `encro_e2e_tool`: impersonates ffmpeg/ffprobe controlled via env vars)
- **Key files:** `tests/e2e/fake_media_tool.cpp` (standalone binary), `tests/e2e/encro_e2e_tests.cpp`

### `plugins/` - xmake Custom Plugins
- **Purpose:** Extend xmake with project-specific tasks.
- **Contains:** `coverge/xmake.lua` (llvm-profdata + llvm-cov workflow), `format/xmake.lua` (clang-format on src/ + tests/)
- **Key files:** `plugins/format/xmake.lua` (invokes clang-format from `D:/clangformat/.clang-format`)

### `.githooks/` - Git Hooks
- **Purpose:** Pre-commit hook for code formatting.
- **Contains:** `pre-commit` script (runs clang-format on staged C/C++ files)
- **Key files:** `.githooks/pre-commit`

## Key File Locations

**Entry Points:**
- `src/main.cpp`: Process entry point — installs crash handlers, calls `appentry::run()`, catches unhandled exceptions
- `tests/test_main.cpp`: Unit/integration test runner (Catch2 session)
- `tests/e2e/e2e_test_main.cpp`: E2E test runner (separate binary)

**Configuration:**
- `xmake.lua`: Build system config — targets (`encro`, `tests`, `e2e_tests`, `encro_e2e_tool`), dependencies, build modes, xpack packaging
- `.gitignore`: Ignore rules for build artifacts, IDE files

**Core Logic:**
- `src/core/app_context.h`: Central `AppContext` struct (configuration + toolchain + runtime state)
- `src/core/error_handle.h`: `eh::Result<T>` type alias and `eh::makeError()` helper
- `src/app/pipeline.cpp`: Top-level routing (video/picture/pack-only)
- `src/video/video_process.cpp`: Video encoding dispatch (530+ lines)
- `src/video/video_batch_execution.cpp`: Parallel encoding execution engine
- `src/pack/packer.cpp`: ZIP creation engine (800+ lines)
- `src/pack/pack_service.cpp`: Pack orchestration with progress (560+ lines)

**Testing:**
- `tests/test_utils.h`: Shared test fixtures and helpers (used by all module tests)
- `tests/`: All test files co-located by module mirroring `src/` structure

## Naming Conventions

**Files:**
- `snake_case` with `.cpp`/`.h` extensions. Examples: `video_info.h`, `job_state.cpp`, `app_entry_tests.cpp`
- Header guards: `#pragma once` exclusively — no `#ifndef`/`#define` guards anywhere
- Headers reside next to their implementations in the same directory. No separate `include/` directory.
- Test files: `*_tests.cpp` for regular tests, `*_standalone_compile_test.cpp` for compile-only tests

**Directories:**
- `snake_case`, lowercase. Examples: `src/app/`, `tests/e2e/`, `src/video/`
- Mirrored structure between `src/` and `tests/`: `src/video/video_info.cpp` is tested by `tests/video_info_tests.cpp`

**Namespaces:**
- Lowercase, no separators, no indent. Examples: `appentry`, `jobstate`, `videobatch`, `collisionnaming`, `stopsignal`, `taskexec`
- Aliases at namespace scope: `namespace fs = std::filesystem;` commonly used.
- Popular alias: `namespace eh = ErrorHandle;` for the `Result`/`makeError` shorthand.

**Types:**
- `PascalCase`. Examples: `AppConfig`, `CmdParseResult`, `PackRequest`, `EncodingState`, `EncodeConfig`, `TaskRecord`

**Functions/Methods:**
- `camelCase`. Examples: `mergeTasks()`, `markRunning()`, `installHandlers()`, `buildConfig()`, `encodeVideo()`
- Trailing return type on ALL functions: `auto fnName(params) -> ReturnType`

**Variables/Members:**
- `camelCase`. Member variables have trailing underscore: `stateFilePath_`, `mtx_`
- Typedef/using aliases: `PascalCase`. Examples: `EncodingStatePtr`, `EncodingStateList`

**Constants:**
- `k` + `PascalCase`. Examples: `kEncodeVideoKind`, `kCanceledExitCode`, `kDefaultMaxArchiveGroupSize`

## Where to Add New Code

**New Feature (e.g., new media processing type like "audio"):**
- Primary code: `src/audio/` (create new directory with `.cpp`/`.h` files)
- Hook into pipeline: `src/app/pipeline.cpp` — add `runAudio()` branch in `pipeline::run()`
- Tests: `tests/audio/` (mirroring the `src/audio/` structure)
- Register test source files in `xmake.lua`: add `add_files("tests/audio/*.cpp")` to the `tests` target

**New Component/Module (e.g., a new CLI subcommand):**
- Implementation: `src/cmd/cmd.cpp` for CLI11 setup, `src/cmd/config_builder.cpp` for config mapping
- Config fields: add to `appctx::AppConfig` in `src/core/app_context.h`
- Tests: `tests/cmd_cmd_tests.cpp` or new file `tests/cmd_newfeature_tests.cpp`

**New Utility Function:**
- Shared helpers: `src/utils/utils.h` (declare) + `src/utils/utils.cpp` (implement)
- Domain-specific helpers that don't warrant a new module: add to the relevant module's header

**New Pack Strategy:**
- Add new enum value to `PackMode`/`NamingStrategy`/`GroupingStrategy` in `src/pack/pack.h`
- Implement logic in `src/pack/packer.cpp` or `src/pack/pack_service.cpp`
- Tests: `tests/packer_tests.cpp` or new `tests/pack_*_tests.cpp`

**New CLI Option:**
- Add field to `CmdParseResult` in `src/cmd/cmd.h`
- Wire CLI11 option in `src/cmd/cmd.cpp` within `commandLineInit()`
- Map to `AppConfig` field in `src/cmd/config_builder.cpp`
- Add corresponding field to `appctx::AppConfig` in `src/core/app_context.h`
- Tests: `tests/cmd_config_builder_tests.cpp`

## Special Directories

### `build/`
- Purpose: xmake build output (object files, binaries, dependency tracking).
- Generated: Yes (by `xmake build`).
- Committed: No (in `.gitignore`).

### `.xmake/`
- Purpose: xmake per-platform configuration cache (toolchains, package resolution).
- Generated: Yes (by `xmake f`).
- Committed: No (in `.gitignore`).

### `.cache/`
- Purpose: clangd language server index cache for IDE support.
- Generated: Yes (by clangd).
- Committed: No.

### `.planning/`
- Purpose: GSD planning artifacts: codebase documentation, phase configs.
- Generated: Yes (by `/gsd:map-codebase` and `/gsd:plan-phase`).
- Committed: Yes (version-controlled for team reference).

### `.claude/`
- Purpose: Claude internal tooling (worktrees for parallel agent execution).
- Generated: Yes.
- Committed: Typically not (project-specific `.gitignore` may apply).

### `plugins/`
- Purpose: xmake custom plugins extending build system with format, coverage commands.
- Generated: No (hand-authored).
- Committed: Yes.

---

*Structure analysis: 2026-05-22*
