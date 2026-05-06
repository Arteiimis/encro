# Codebase Structure

**Analysis Date:** 2026-05-07

## Directory Layout

```
encro/
├── .cache/                  # Xmake package cache (not committed)
├── .githooks/               # Git hooks
│   └── pre-commit           # Pre-commit hook script
├── .planning/               # Project planning documents
│   ├── codebase/            # Codebase analysis documents (this directory)
│   ├── phases/              # Phase implementation plans
│   ├── milestones/          # Milestone tracking
│   ├── PROJECT.md           # Project overview
│   ├── REQUIREMENTS.md      # Requirements specification
│   ├── ROADMAP.md           # Roadmap
│   └── ...
├── .vscode/                 # VS Code workspace settings (not committed)
│   └── settings.json
├── .xmake/                  # Xmake local cache (not committed)
├── build/                   # Build output (not committed)
├── plugins/                 # Xmake plugins (custom tasks)
│   ├── coverge/             # Coverage reporting plugin
│   │   └── xmake.lua
│   └── format/              # Clang-format plugin
│       └── xmake.lua
├── src/                     # Production source code
│   ├── main.cpp             # Entry point: main()
│   ├── app/                 # Application entry & pipeline
│   ├── cmd/                 # CLI parsing & config building
│   ├── core/                # Shared core abstractions
│   ├── infra/               # Infrastructure (terminal, crash, signals)
│   ├── pack/                # ZIP packing subsystem
│   ├── picture/             # Picture processing (compress & pack)
│   ├── utils/               # Utility functions (exec, ffmpeg find)
│   └── video/               # Video processing (scan, encode, progress)
├── tests/                   # Test suite
│   ├── test_main.cpp        # Test runner entry point
│   ├── test_utils.h         # Shared test utilities
│   ├── app/                 # Tests for app/ module
│   ├── e2e/                 # End-to-end tests
│   ├── infra/               # Tests for infra/ module
│   ├── picture/             # Tests for picture/ module
│   ├── video/               # Tests for video/ module
│   └── *.cpp                # Flat test files for core modules
└── xmake.lua                # Build configuration
```

## Directory Purposes

**`src/app/`:**
- Purpose: Application entry point and pipeline orchestration
- Contains: `main.cpp` delegates to `app_entry.cpp`; `pipeline.cpp` routes processing; `prelude.cpp` handles startup init (logging, terminal, arg parsing)
- Key files: `src/app/app_entry.cpp`, `src/app/pipeline.cpp`, `src/app/prelude.cpp`

**`src/cmd/`:**
- Purpose: CLI argument parsing and configuration construction
- Contains: `cmd.cpp` — `commandLineInit()` parses args via Boost.ProgramOptions; `config_builder.cpp` — `cmd::buildConfig()` transforms raw options into `appctx::AppConfig`
- Key files: `src/cmd/cmd.h`, `src/cmd/config_builder.h`

**`src/core/`:**
- Purpose: Shared abstractions used by all layers — app context, job state, media scanning, progress bars, task execution, naming utilities
- Contains: Header+source pairs for stateful modules; header-only for inline utilities (`collision_naming.h`, `display_text.h`, `path_roots.h`); error handling alias (`error_handle.h` — `eh::Result<T>`)
- Key files: `src/core/app_context.h` (central `AppContext` struct), `src/core/job_state.h` (resumable state), `src/core/task_executor.h` (parallel task runner), `src/core/progress.h` (progress bars), `src/core/media_scanner.h` (file scanning)

**`src/infra/`:**
- Purpose: OS-level infrastructure — crash handling, terminal I/O, stop signals, stack traces, toolchain discovery, console width
- Contains: All have `.h` + `.cpp` pairs; Windows-specific implementations guarded with `#if defined(_WIN32)`
- Key files: `src/infra/terminal.h` (styled output), `src/infra/crash_runtime.h` (SEH/signal handlers), `src/infra/toolchain.h` (ffmpeg/ffprobe resolution), `src/infra/stop_signal.h` (Ctrl+C handling)

**`src/pack/`:**
- Purpose: ZIP archive creation — file grouping, naming strategies, progress callbacks, resumable packing
- Contains: `pack.h` (public API — `PackRequest`, `PackMode`, `NamingConfig`, `pack::execute()`); `packer.h` (ZIP I/O via libzippp, file grouping algorithms); `pack_service.h` (mid-level orchestrator); `pack_types.h` (shared value types); `pack_plan_internal.h` (internal `PackPlan`); `pack_internal.h` (internal helpers); `packer_types.h` (internal detail types)
- Key files: `src/pack/pack.h`, `src/pack/pack.cpp`, `src/pack/packer.h`, `src/pack/pack_service.h`

**`src/video/`:**
- Purpose: Video encoding workflow — scanning, output planning, batch execution, progress parsing, ffmpeg command building
- Contains: `video_info.h` (scanning, ffprobe queries); `video_output_planning.h` (map inputs → planned output paths); `video_process.h` (orchestration: scan → encode → pack); `video_batch_execution.h` (threaded encoding with progress); `video_encode_runner.h` (single encode execution); `encode_config.h` (ffmpeg command builder); `video_progress_parser.h` (ffmpeg progress file parser); `video_workflow_utils.h` (job state access helpers)
- Key files: `src/video/video_process.cpp`, `src/video/video_batch_execution.h`, `src/video/video_encode_runner.cpp`, `src/video/encode_config.h`

**`src/picture/`:**
- Purpose: Picture processing — scanning, compression (to JPEG via ffmpeg), zip entry planning, pack orchestration
- Contains: `picture_process.h` (scan + pack workflow); `picture_compress.h` (compress single/batch images)
- Key files: `src/picture/picture_process.cpp`, `src/picture/picture_compress.cpp`

**`src/utils/`:**
- Purpose: General-purpose utilities — subprocess execution, ffmpeg/ffprobe discovery, user input prompts, UUID generation
- Contains: Single `utils.h` + `utils.cpp` pair
- Key files: `src/utils/utils.h`

**`tests/`:**
- Purpose: Unit tests, module tests, and end-to-end tests using Catch2
- Contains: `test_main.cpp` (standard test runner); `test_utils.h` (shared helpers); subdirectories mirror `src/` structure (`app/`, `infra/`, `picture/`, `video/`, `e2e/`); flat `.cpp` files for core module tests
- Key files: `tests/test_main.cpp`, `tests/e2e/encro_e2e_tests.cpp`

**`plugins/`:**
- Purpose: Xmake build system plugins — custom tasks
- Contains: `coverge/` — runs tests under LLVM coverage, merges `.profraw`, reports via `llvm-cov`; `format/` — runs `clang-format` across `src/` and `tests/` with a config file
- Key files: `plugins/coverge/xmake.lua`, `plugins/format/xmake.lua`

## Key File Locations

**Entry Points:**
- `src/main.cpp`: `main()` — crash handler install, delegates to `appentry::run()`
- `tests/test_main.cpp`: Catch2 test runner for unit/module tests
- `tests/e2e/e2e_test_main.cpp`: Catch2 test runner for end-to-end tests

**Configuration:**
- `xmake.lua`: Build system root — compiler flags, dependencies, targets (`encro`, `tests`, `e2e_tests`, `encro_e2e_tool`)
- `.vscode/settings.json`: VS Code editor settings (not committed)
- `.githooks/pre-commit`: Pre-commit hook

**Core Logic:**
- `src/core/app_context.h`: Central `AppContext`, `AppConfig`, `RuntimeContext` structs
- `src/core/job_state.h`: `jobstate::Store` — resumable job state persistence
- `src/core/task_executor.h`: `taskexec::runTasks()` — generic parallel task execution
- `src/core/error_handle.h`: `eh::Result<T>` (alias for `std::expected<T, std::string>`)
- `src/app/pipeline.cpp`: Main dispatch logic — routes `processType` to video/picture/pack-only

**Testing:**
- `tests/test_main.cpp`: Test runner
- `tests/*.cpp`: Flat test files (e.g., `packer_tests.cpp`, `media_scanner_tests.cpp`)
- `tests/app/`: Tests for entry/pipeline
- `tests/video/`: Tests for video encoding pipelines
- `tests/picture/`: Tests for picture workflows
- `tests/infra/`: Tests for infra (terminal, stacktrace, crash)
- `tests/e2e/`: End-to-end integration tests with fake media tool

**Build Output:**
- `build/`: All build artifacts (.obj, .exe, coverage .profraw) — not committed

## Naming Conventions

**Files:**
- `snake_case` for all source and header files: `video_process.cpp`, `job_state.h`, `config_builder.cpp`
- Header and source use identical stem: `pack.h` / `pack.cpp`, `terminal.h` / `terminal.cpp`
- Internal headers marked with `_internal` suffix: `pack_plan_internal.h`, `pack_internal.h`
- Detail headers: `packer_types.h`, `job_state_detail.h`
- Test files: `{module}_tests.cpp` or `{specific_feature}_tests.cpp` (e.g., `packer_tests.cpp`, `video_progress_parser_tests.cpp`)

**Directories:**
- `snake_case`: `src/core/`, `src/video/`, `tests/e2e/`
- Mirror between `src/` and `tests/`: `src/app/` ↔ `tests/app/`, `src/video/` ↔ `tests/video/`

**Namespaces:**
- `snake_case` or short lowercase: `appentry`, `pipeline`, `pack`, `jobstate`, `videobatch`, `taskexec`, `collisionnaming`, `displaytext`, `stopsignal`, `toolchain`, `pathroots`, `videoworkflow`
- Alias for convenience: `namespace eh = ErrorHandle;`, `namespace fs = std::filesystem;` (in each file)
- Flat nesting — no deeply nested namespaces beyond 1-2 levels

**Functions:**
- `camelCase`: `handlePathEncoding()`, `buildConfig()`, `scanByExtensions()`, `runPackOnly()`
- `snake_case` for some infra: `installHandlers()`, `requestStop()`
- File-level free functions in anonymous namespaces: `scanInputVideos()`, `packEncodedVideos()`

**Types (Structs/Classes/Enums):**
- `PascalCase`: `AppContext`, `PackRequest`, `TaskRecord`, `EncodingState`
- Enum class values: `PascalCase` — `PackMode::Media`, `OutputLayout::Keep`, `TaskStatus::Succeeded`
- Enum class names: `PascalCase` — `PackMode`, `TaskStatus`, `MessageKind`

**Constants:**
- `k` prefix + `PascalCase`: `kDefaultMaxArchiveGroupSize`, `kEncodeVideoKind`, `kCanceledExitCode`

## Where to Add New Code

**New Feature (e.g., new media processing type):**
- Primary code: `src/{feature}/` — new directory with headers and implementation
- Pipeline routing: Add branch in `src/app/pipeline.cpp` → `pipeline::run()`
- Config: Add fields to `appctx::AppConfig` in `src/core/app_context.h`; add CLI parsing in `src/cmd/config_builder.cpp`
- Tests: `tests/{feature}/`

**New Video Codec or Output Format:**
- Encode config: `src/video/encode_config.h` — add to `buildCMD()`
- Validation: `EncodeConfig::validate()` — add to `validOutputFormats` array
- Output planning: `src/video/video_output_planning.cpp` — add target extension mapping
- Packing (if output differs): `src/video/video_process.cpp` — `collectEncodedOutputFiles()`

**New Packing Strategy:**
- Add enum variant to `NamingStrategy` in `src/pack/pack.h`
- Add naming logic branch in `src/pack/pack.cpp` → `collectPackInputs()`
- Add grouping variant to `GroupingStrategy` in `src/pack/pack.h`
- Add grouping logic in `src/pack/packer.cpp` → `groupPackEntries()`
- Tests: `tests/packer_tests.cpp` or `tests/pack_plan_boundary_test.cpp`

**New CLI Option:**
- Add to options description in `src/cmd/cmd.cpp` → `commandLineInit()`
- Add field to `appctx::AppConfig` in `src/core/app_context.h`
- Add parsing in `src/cmd/config_builder.cpp` → `cmd::buildConfig()`
- Add logging in `src/app/prelude.cpp` → `logConfigSummary()`

**New Utility Function:**
- General-purpose: `src/utils/utils.h` + `src/utils/utils.cpp`
- Naming/path-related: `src/core/collision_naming.h` or `src/core/path_roots.h`
- Terminal-related: `src/infra/terminal.h` + `src/infra/terminal.cpp`

**New Test Suite:**
- Module tests: `tests/{module}/{feature}_tests.cpp`
- Flat core tests: `tests/{feature}_tests.cpp`
- Register files in `xmake.lua` under `target("tests")` → `add_files("tests/{module}/*.cpp")`

## Special Directories

**`.cache/`:**
- Purpose: Xmake package download cache
- Generated: Yes (by xmake)
- Committed: No (in `.gitignore`)

**`.xmake/`:**
- Purpose: Xmake local state (build config cache)
- Generated: Yes
- Committed: No

**`build/`:**
- Purpose: All build artifacts — object files, binaries, coverage data
- Generated: Yes
- Committed: No

**`.vscode/`:**
- Purpose: VS Code editor settings
- Generated: No (manually created)
- Committed: No (in `.gitignore`)

**`.planning/`:**
- Purpose: GSD planning documents, phases, milestones, retrospective
- Generated: No (authored)
- Committed: Yes

---

*Structure analysis: 2026-05-07*
