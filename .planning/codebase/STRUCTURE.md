---
focus: arch
last_mapped_commit: 919b0cea076d2821618c3febf54f72285880cd4c
mapped_at: 2026-04-26
---

# Codebase Structure

**Analysis Date:** 2026-04-26

## Directory Layout

```
encro/
├── src/                        # Production source code
│   ├── main.cpp                # Program entry point
│   ├── app/                    # Application layer (entry, prelude, pipeline)
│   ├── cmd/                    # CLI parsing and config building
│   ├── video/                  # Video processing domain
│   ├── picture/                # Picture processing domain
│   ├── pack/                   # Archive (zip) creation
│   ├── core/                   # Core abstractions and shared types
│   ├── infra/                  # Infrastructure (platform abstractions, I/O)
│   └── utils/                  # General utilities
├── tests/                      # Test suite
│   ├── test_main.cpp           # Unit/integration test entry (Catch2)
│   ├── test_utils.h            # Shared test helpers (temp dirs, etc.)
│   ├── *.cpp                   # Top-level test files
│   ├── app/                    # Tests for app/ layer
│   ├── infra/                  # Tests for infra/ layer
│   ├── picture/                # Tests for picture/ layer
│   ├── video/                  # Tests for video/ layer
│   └── e2e/                    # End-to-end tests
│       ├── e2e_test_main.cpp   # E2E test entry
│       ├── e2e_test_utils.h    # E2E shared helpers
│       ├── e2e_test_utils.cpp
│       ├── encro_e2e_tests.cpp # E2E test cases
│       └── fake_media_tool.cpp # Fake media tool binary
├── plugins/                    # xmake custom tasks
│   ├── format/xmake.lua        # clang-format task
│   └── coverge/xmake.lua       # Coverage task
├── plans/                      # Working planning documents
├── .planning/                  # GSD planning artifacts
├── .githooks/                  # Git hooks (pre-commit clang-format)
├── .vscode/                    # VS Code settings (gitignored)
├── .xmake/                     # xmake cache (gitignored)
├── build/                      # Build output (gitignored)
├── xmake.lua                   # Build configuration
├── AGENTS.md                   # Agent behavioral guidelines
└── .gitignore
```

## Directory Purposes

**src/app/:** Application layer -- entry point coordination, startup bootstrap, workflow orchestration.
- Contains: `app_entry.h/.cpp`, `prelude.h/.cpp`, `pipeline.h/.cpp`
- Key: `src/app/app_entry.cpp` (CLI->config->toolchain->pipeline wiring), `src/app/pipeline.cpp` (video/picture/pack-only dispatch)

**src/cmd/:** CLI argument parsing and typed config construction.
- Contains: `cmd.h/.cpp` (parsing), `config_builder.h/.cpp` (config construction)
- Key: `src/cmd/config_builder.cpp` (328 lines -- all CLI option-to-config mapping)

**src/video/:** Video processing domain -- scanning, encoding, output planning, progress monitoring.
- Contains: `video_process`, `video_batch_execution`, `video_encode_runner`, `video_info`, `video_output_planning`, `video_progress_parser`, `video_workflow_utils`
- Key: `src/video/video_batch_execution.cpp` (654 lines), `src/video/video_process.cpp` (533 lines)

**src/picture/:** Picture processing domain -- scanning, compression, pack planning.
- Contains: `picture_process.h/.cpp`, `picture_compress.h/.cpp`
- Key: `src/picture/picture_process.cpp` (521 lines)

**src/pack/:** Zip archive creation -- file grouping, pack plan execution.
- Contains: `packer.h/.cpp`, `pack_service.h/.cpp`
- Key: `src/pack/packer.cpp` (657 lines -- largest file)

**src/core/:** Shared types, error handling, task execution, state persistence, utilities.
- Contains: `app_context`, `error_handle`, `task_executor`, `parallel`, `progress`, `job_state` + `job_state_store` + `job_state_detail`, `media_scanner`, `collision_naming`, `display_text`, `archive_plan`, `path_roots`
- Key: `src/core/app_context.h` (central config structs), `src/core/job_state.cpp` (570 lines), `src/core/error_handle.h` (eh::Result)

**src/infra/:** Platform infrastructure -- crash handling, terminal I/O, signal handling, tool resolution.
- Contains: `crash_runtime`, `stacktrace`, `terminal`, `stop_signal`, `toolchain`, `console_width`
- Key: `src/infra/terminal.h` (colored output), `src/infra/crash_runtime.cpp` (SEH handlers)

**src/utils/:** General utilities -- subprocess execution, user input.
- Contains: `utils.h/.cpp`
- Key: `src/utils/utils.cpp` (exec2 subprocess runner)

**tests/:** All test code, mirroring src/ structure. Catch2 test binaries.
- Key: `tests/test_main.cpp` (unit entry), `tests/e2e/e2e_test_main.cpp` (E2E entry)

**tests/e2e/:** End-to-end tests exercising the full encro binary.
- Contains: test cases, shared helpers, `fake_media_tool.cpp` (separate binary)

**plugins/:** xmake custom task plugins for format and coverage workflows.

**plans/:** Human-authored planning documents for future refactoring/optimization.

## Key File Locations

**Entry Points:**
- `src/main.cpp`: Program entry -- installs crash handlers, delegates to `appentry::run()`
- `tests/test_main.cpp`: Unit/integration test binary entry (Catch2)
- `tests/e2e/e2e_test_main.cpp`: End-to-end test binary entry (Catch2)

**Configuration:**
- `xmake.lua`: Build config -- toolchain (clang-cl), C++26, packages, targets, packaging
- `src/core/app_context.h`: `AppConfig`, `ToolchainPaths`, `RuntimeContext`, `AppContext` structs
- `src/cmd/config_builder.cpp`: CLI options to `AppConfig` mapping (328 lines)

**Core Logic (largest implementation files):**
- `src/pack/packer.cpp` (657 lines): Zip creation, file grouping, directory pack workflows
- `src/video/video_batch_execution.cpp` (654 lines): Parallel batch encoding, progress bars, monitor
- `src/core/job_state.cpp` (570 lines): Persistent state store (JSON snapshot read/write/merge)
- `src/video/video_process.cpp` (533 lines): Video workflow orchestration
- `src/picture/picture_process.cpp` (521 lines): Picture scan and pack workflow
- `src/video/video_info.cpp` (482 lines): ffprobe parsing, video scanning

**Testing helpers:**
- `tests/test_utils.h`: Shared test utilities (temp directories, file creation)
- `tests/e2e/e2e_test_utils.h/.cpp`: E2E test helpers
- 32 test files total across unit, integration, and E2E suites

## Naming Conventions

**Files:** `snake_case` throughout: `video_batch_execution.h`, `job_state_store.cpp`
- Header-only modules are `.h` only: `error_handle.h`, `collision_naming.h`, `display_text.h`, `path_roots.h`
- Test files: `{module}_tests.cpp` mirroring source module name

**Directories:** `snake_case`: `src/video/`, `tests/e2e/`
- Tests subdirectories mirror src/ structure: `tests/video/` <-> `src/video/`

**Namespaces:** `snake_case`: `appentry`, `pipeline`, `jobstate`, `taskexec`, `collisionnaming`
- Common aliases: `namespace fs = std::filesystem;`, `namespace eh = ErrorHandle;`, `namespace po = boost::program_options;`

**Types:** `PascalCase` for structs/classes/enums: `AppConfig`, `TaskPlan`, `MessageKind`, `Tone`
- Enum values: `OutputLayout::Flat`, `OutputLayout::Keep`, `TaskStatus::Pending`

**Functions:** `camelCase`: `buildConfig()`, `runEncodingTasks()`, `handlePathEncoding()`, `readUserIpt()`
- Boolean predicates: `should*`, `is*`, `has*` prefix
- `auto` return with trailing return type heavily used: `auto run(argc, argv) -> int;`

**Variables:** Mixed styles -- `camelCase` for locals (`vidPath`, `progressCtx`), trailing `_` for private members (`stateFilePath_`, `snapshot_`), `k` prefix for constants (`kVideoTypes`, `kCanceledExitCode`)

## Where to Add New Code

**New feature (new media type like "audio"):**
- Primary code: `src/audio/` (new directory, following `video/` or `picture/` pattern)
- Tests: `tests/audio/{feature}_tests.cpp`
- Register in `xmake.lua` test target
- Wire into `src/app/pipeline.cpp::run()` with new process type branch

**New CLI option:**
- Define option: `src/cmd/cmd.cpp` in `commandLineInit()`
- Add config field: `src/core/app_context.h` -> `AppConfig` struct
- Parse value: `src/cmd/config_builder.cpp` -> `cmd::buildConfig()`
- Log if needed: `src/app/prelude.cpp` -> `logConfigSummary()`
- Test: `tests/cmd_config_builder_tests.cpp`

**New core utility:**
- Pure logic: header-only `src/core/{name}.h` (follow `collision_naming.h` pattern)
- Stateful/large: `src/core/{name}.h` + `src/core/{name}.cpp`
- No build changes needed: `add_files("src/**.cpp")` auto-discovers

**New infrastructure component:**
- `src/infra/{name}.h` + `src/infra/{name}.cpp`
- Tests: `tests/infra/{name}_tests.cpp`
- Auto-discovered by xmake glob patterns

## Special Directories

**build/:** xmake build output. Generated. Not committed (`.gitignore`).

**.xmake/:** xmake package cache and platform config. Generated. Not committed.

**.vscode/:** VS Code settings. Manual. Not committed.

**.planning/:** GSD planning artifacts (codebase maps, plans). Generated by tools. Committed.

**plans/:** Human-authored planning documents. Manual. Committed.

**.githooks/:** Git hooks. Manual. Committed. Contains clang-format pre-commit hook.

---

*Structure analysis: 2026-04-26*
