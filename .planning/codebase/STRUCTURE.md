# Codebase Structure

**Analysis Date:** 2026-04-28

## Directory Layout

```
[project-root]/
├── .claude/                  # Claude AI configuration (if present)
├── .githooks/                # Git hooks
│   └── pre-commit            # Pre-commit hook script
├── .planning/                # GSD planning artifacts (not committed patterns)
│   ├── codebase/             # Codebase map documents (ARCHITECTURE.md, etc.)
│   ├── milestones/           # Milestone roadmaps (v1.0, v1.1)
│   ├── phases/               # Phase planning artifacts
│   ├── quick/                # Quick-fix plans
│   ├── STATE.md              # Current project state
│   ├── PROJECT.md            # Project description
│   ├── ROADMAP.md            # Overall roadmap
│   └── config.json           # Planning configuration
├── .vscode/                  # VS Code workspace settings
│   └── settings.json
├── .xmake/                   # Xmake build cache (gitignored)
├── build/                    # Build output directory (gitignored)
│   ├── .gens/                # Generated build rules
│   ├── compile_commands.json # LSP compile commands
│   ├── debug_probe/          # Debug probe data (sample files)
│   └── windows/x64/          # Platform-specific builds
│       ├── release/          # Release build outputs
│       └── releasedbg/       # Release+debug build outputs
├── plans/                    # Implementation plans & checklists
│   ├── CODE_REUSE_OPTIMIZATION_PLAN.md
│   ├── VIDEO_ENCODE_PERF_OPTIMIZATION_PLAN.md
│   └── VIDEO_PROCESS_REFACTOR_CHECKLIST.md
├── plugins/                  # Xmake plugins
│   ├── coverge/              # Coverage plugin
│   └── format/               # Format plugin
├── src/                      # Source code
│   ├── main.cpp              # Entry point
│   ├── app/                  # Application layer
│   ├── cmd/                  # CLI parsing
│   ├── core/                 # Core domain types & logic
│   ├── infra/                # Platform/infrastructure abstractions
│   ├── pack/                 # ZIP packing service
│   ├── picture/              # Picture processing domain
│   ├── utils/                # General utilities
│   └── video/                # Video processing domain
├── tests/                    # Test code
│   ├── test_main.cpp         # Unit test entry point
│   ├── test_utils.h          # Test utilities & helpers
│   ├── app/                  # App-layer tests
│   ├── e2e/                  # End-to-end tests
│   ├── infra/                # Infrastructure tests
│   ├── picture/              # Picture domain tests
│   ├── video/                # Video domain tests
│   └── *.cpp                 # Top-level tests (core components)
├── .gitignore
└── xmake.lua                 # Build configuration
```

## Directory Purposes

**`src/`:**
- Purpose: All production source code
- Contains: `.cpp` implementation files and `.h` header files, organized into 9 subdirectories by layer/domain
- Key files: `main.cpp` (entry point), `app/app_entry.cpp` (orchestration), `app/pipeline.cpp` (workflow dispatch)

**`src/app/`:**
- Purpose: Application bootstrap, startup orchestration, pipeline dispatch
- Contains: `app_entry.cpp/.h`, `prelude.cpp/.h`, `pipeline.cpp/.h`
- Key files: `app_entry.cpp` (startup lifecycle), `pipeline.cpp` (routes to video/picture/pack workflows)

**`src/cmd/`:**
- Purpose: Command-line argument definition and parsing
- Contains: `cmd.cpp/.h` (Boost.ProgramOptions setup), `config_builder.cpp/.h` (argument → config translation)
- Key files: `config_builder.cpp` (13KB, all CLI option handling)

**`src/core/`:**
- Purpose: Shared domain types, configuration, state management, generic task execution, progress display
- Contains: 19 files — `app_context.h`, `job_state.cpp/.h`, `task_executor.cpp/.h`, `progress.cpp/.h`, `media_scanner.cpp/.h`, `parallel.cpp/.h`, `archive_plan.cpp/.h`, plus header-only utilities (`collision_naming.h`, `display_text.h`, `error_handle.h`, `path_roots.h`, `job_state_detail.h`)
- Key files: `app_context.h` (central types), `job_state.cpp` (23KB, resumable state), `progress.cpp` (7.6KB)

**`src/video/`:**
- Purpose: Video encoding domain — scanning, metadata extraction, output planning, batch encoding, progress parsing
- Contains: 14 files — `video_process.cpp/.h`, `video_batch_execution.cpp/.h`, `video_encode_runner.cpp/.h`, `video_info.cpp/.h`, `video_output_planning.cpp/.h`, `video_progress_parser.cpp/.h`, `encode_config.h`, `video_workflow_utils.h`
- Key files: `video_process.cpp` (17KB, workflow orchestration), `video_batch_execution.cpp` (25KB, parallel encode execution), `video_info.cpp` (15KB, ffprobe-based metadata)

**`src/picture/`:**
- Purpose: Picture processing domain — scanning, compression, packaging
- Contains: `picture_process.cpp/.h`, `picture_compress.cpp/.h`
- Key files: `picture_process.cpp` (21KB, full picture workflow)

**`src/pack/`:**
- Purpose: ZIP archive creation and pack plan execution
- Contains: `packer.cpp/.h`, `pack_service.cpp/.h`
- Key files: `packer.cpp` (25KB, low-level ZIP operations), `pack_service.cpp` (11KB, high-level plan execution)

**`src/infra/`:**
- Purpose: OS/platform abstractions — terminal I/O, crash handling, toolchain discovery, signal handling
- Contains: `terminal.cpp/.h`, `crash_runtime.cpp/.h`, `toolchain.cpp/.h`, `stop_signal.cpp/.h`, `stacktrace.cpp/.h`, `console_width.cpp/.h`
- Key files: `terminal.cpp` (7.5KB, colored output), `crash_runtime.cpp` (3.2KB, Windows SEH)

**`src/utils/`:**
- Purpose: Generic utilities with no domain knowledge — process execution, user prompts, path discovery
- Contains: `utils.cpp/.h`
- Key files: `utils.cpp` (11KB)

**`tests/`:**
- Purpose: All test code, mirroring `src/` structure
- Contains: 34 files organized into `app/`, `e2e/`, `infra/`, `picture/`, `video/` subdirectories plus top-level files
- Key files: `test_main.cpp` (entry), `test_utils.h` (shared test helpers: `TempDir`, file helpers)

**`tests/e2e/`:**
- Purpose: End-to-end integration tests
- Contains: `encro_e2e_tests.cpp`, `e2e_test_utils.cpp/.h`, `e2e_test_main.cpp`, `fake_media_tool.cpp`
- Key files: `encro_e2e_tests.cpp` (16.6KB), `fake_media_tool.cpp` (generates test media)

**`build/`:**
- Purpose: Build outputs — binaries, compile commands, debug data
- Generated: Yes (by Xmake)
- Committed: No (gitignored)

**`.xmake/`:**
- Purpose: Xmake build cache and package dependencies
- Generated: Yes
- Committed: No (gitignored)

**`plugins/`:**
- Purpose: Xmake build system plugins
- Contains: `coverge/` (coverage instrumentation), `format/` (code formatting)
- Key files: Plugin Lua scripts (not explored in detail)

**`.planning/`:**
- Purpose: GSD workflow planning artifacts — roadmaps, phase plans, milestone audits
- Contains: `STATE.md`, `PROJECT.md`, `ROADMAP.md`, `milestones/`, `phases/`, `quick/`, `codebase/`
- Key files: `ROADMAP.md` (project direction), `config.json` (planning config)

**`plans/`:**
- Purpose: Human-authored implementation plans and checklists
- Contains: Code reuse optimization plan, video encode perf plan, video process refactor checklist
- Key files: `VIDEO_PROCESS_REFACTOR_CHECKLIST.md`

## Key File Locations

**Entry Points:**
- `src/main.cpp`: Process entry point — crash handlers, catch-all, delegate to `appentry::run()`
- `tests/test_main.cpp`: Unit test entry point (`#define CATCH_CONFIG_MAIN`)
- `tests/e2e/e2e_test_main.cpp`: E2E test entry point

**Configuration:**
- `xmake.lua`: Build configuration — targets, dependencies, flags, packaging
- `.vscode/settings.json`: Editor settings
- `.gitignore`: Git exclusion rules

**Core Logic (largest files):**
- `src/pack/packer.cpp` (25KB): ZIP creation, file grouping algorithms
- `src/video/video_batch_execution.cpp` (25KB): Parallel encode orchestration with progress
- `src/core/job_state.cpp` (23KB): Resumable state persistence and merge
- `src/picture/picture_process.cpp` (21KB): Picture scan, compress, pack workflow
- `src/video/video_process.cpp` (17KB): Video workflow orchestration
- `src/video/video_info.cpp` (15KB): ffprobe-based video metadata extraction
- `src/cmd/config_builder.cpp` (13KB): CLI argument → AppConfig translation

**Shared Types (header-only):**
- `src/core/app_context.h` (3.3KB): AppConfig, ToolchainPaths, RuntimeContext, AppContext, EncodingState
- `src/core/collision_naming.h` (5KB): Conflict-safe file naming with FNV-1a hashing
- `src/core/error_handle.h` (0.4KB): `eh::Result<T>` and `eh::makeError()`
- `src/video/encode_config.h` (3.7KB): ffmpeg command construction struct
- `src/core/display_text.h` (2.5KB): Unicode text truncation
- `src/video/video_workflow_utils.h` (1.2KB): Workflow helper templates

**Testing:**
- `tests/test_utils.h`: Shared test utilities (`TempDir`, `writeFile`, `touchFile`, zip inspection)
- `tests/test_main.cpp`: Catch2 main
- `tests/e2e/e2e_test_utils.h`: E2E-specific test helpers
- `tests/e2e/fake_media_tool.cpp`: Media generator tool built as separate `encro_e2e_tool` target

## Naming Conventions

**Files:**
- All lowercase with underscores: `video_batch_execution.cpp`, `job_state.h`, `app_context.h`
- Header/implementation pairs: `foo.h` + `foo.cpp`
- Header-only utilities: `foo.h` with no corresponding `.cpp` (e.g., `collision_naming.h`, `display_text.h`, `error_handle.h`)

**Directories:**
- All lowercase, single words: `app/`, `cmd/`, `core/`, `infra/`, `pack/`, `picture/`, `utils/`, `video/`
- Test subdirectories mirror source structure: `tests/app/`, `tests/video/`, `tests/picture/`, `tests/infra/`, `tests/e2e/`

**Namespaces:**
- Short, lowercase names matching directory/module: `appentry`, `appctx`, `jobstate`, `taskexec`, `progress`, `media`, `pack`, `toolchain`, `terminal`, `crash`, `stopsignal`, `videobatch`, `videoworkflow`, `displaytext`, `collisionnaming`, `pathroots`, `archiveplan`
- Convenience alias: `namespace eh = ErrorHandle;` defined in `src/core/error_handle.h`
- Common aliases: `namespace fs = std::filesystem;`, `namespace json = boost::json;`, `namespace po = boost::program_options;`

**Functions:**
- camelCase for public API: `run()`, `buildConfig()`, `initStartup()`, `readAllVids()`, `planVideoOutputFiles()`
- camelCase for private/internal: `scanInputVideos()`, `buildEncodeActions()`, `prepareEncodeActions()`
- `auto` return type with trailing return type (`-> Type`) used pervasively

**Variables:**
- camelCase: `stateFilePath`, `maxParallelJobs`, `outputLayout`
- Private members with trailing underscore: `stateFilePath_`, `snapshot_`, `mtx_`, `lastFlushAtMs_`

**Types (structs/classes/enums):**
- PascalCase: `AppContext`, `AppConfig`, `EncodingState`, `TaskPlan`, `PackPlan`, `EncodeConfig`
- Enum values PascalCase: `TaskStatus::Pending`, `OutputLayout::Flat`, `Tone::Default`

**Constants:**
- `k` prefix PascalCase: `kEncodeVideoKind`, `kBuildArchiveKind`, `kCanceledExitCode`, `kDefaultMaxArchiveGroupSize`

## Where to Add New Code

**New feature (e.g., new media type "audio"):**
- Primary code: `src/audio/` — create `audio_process.cpp/.h`, `audio_info.cpp/.h`, etc.
- Tests: `tests/audio/` — create test files following existing conventions
- Integration: Add `runAudio(ctx)` branch in `src/app/pipeline.cpp`
- Build: Add `src/audio/*.cpp` file pattern to `xmake.lua` `encro` target

**New CLI option:**
- Add field to `appctx::AppConfig` in `src/core/app_context.h`
- Add option definition in `src/cmd/cmd.cpp`
- Add translation logic in `src/cmd/config_builder.cpp`
- Add test case in `tests/cmd_config_builder_tests.cpp`

**New pack variant/strategy:**
- Implementation: `src/pack/` — add function to `packer.cpp/.h` or new file
- High-level orchestration: Add to `pack_service.cpp/.h` if exposing a new `PackPlan` variant
- Tests: `tests/packer_tests.cpp` or `tests/pack_service_tests.cpp`

**New infrastructure capability (e.g., new OS integration):**
- Implementation: `src/infra/` — new `.cpp/.h` pair
- Tests: `tests/infra/` — new test file
- Internal only; no changes needed in layers above

**Utilities:**
- Shared helpers: `src/utils/utils.cpp/.h` — add free functions
- If the utility file grows beyond ~15KB, split into domain-specific utility files (e.g., `path_utils.cpp`, `string_utils.cpp`)

**New test target or test category:**
- Add test `.cpp` files in `tests/` or subdirectory
- Update `xmake.lua` `tests` target `add_files(...)` patterns
- For standalone test tools (like `encro_e2e_tool`), add new `target(...)` block in `xmake.lua`

## Special Directories

**`build/`:**
- Purpose: Build artifacts (binaries, compile commands, probe data)
- Generated: Yes (by Xmake)
- Committed: No (gitignored)

**`.xmake/`:**
- Purpose: Xmake cache, package dependencies, platform configuration
- Generated: Yes (by Xmake)
- Committed: No (gitignored)

**`.vscode/`:**
- Purpose: Editor workspace settings
- Generated: No (manually created)
- Committed: No (gitignored by `.gitignore`)

**`plugins/`:**
- Purpose: Xmake build system plugins (coverage instrumentation, code formatting)
- Generated: No
- Committed: Yes

**`.githooks/`:**
- Purpose: Custom git hooks (pre-commit)
- Generated: No
- Committed: Yes

**`.planning/`:**
- Purpose: GSD workflow planning documents and codebase maps
- Generated: Yes (by GSD commands)
- Committed: Yes (tracked for team context sharing)

---

*Structure analysis: 2026-04-28*
