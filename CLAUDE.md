# AGENTS.md — encrō

## Build & Run

- **Build system:** xmake (not CMake). Toolchain: `clang-cl` + `lld-link` on Windows. C++26.
- **Build:** `xmake build encro`
- **Tests (unit/integration):** `xmake build tests && xmake run tests`
- **Tests (e2e):** `xmake build e2e_tests && xmake run e2e_tests`
  - E2E tests depend on `encro` + `encro_e2e_tool` (fake ffmpeg/ffprobe) binaries. Build `encro` first.
- **Single test:** `xmake run tests "[tag-name]"` (Catch2 tag filter)
- **Coverage:** `xmake f -m coverage && xmake build tests && LLVM_PROFILE_FILE=build/coverage/tests-%p.profraw xmake run tests`
  - Or use the plugin: `xmake coverage` (see below)
- **Format:** `xmake format` (apply), `xmake format -k check` (CI/check-only)
- **ASan:** Debug mode enables Address Sanitizer (`xmake f -m debug`)

### xmake Custom Plugins

- `xmake format` — clang-format on all `src/` and `tests/` C/C++ files (config at `D:/clangformat/.clang-format`, NOT in repo)
- `xmake coverage` — configure→build→run→merge .profraw→report. Requires `llvm-profdata` + `llvm-cov` on PATH. Resets config to release mode after.

### Build Modes

| Mode         | Flags                                         |
| ------------ | --------------------------------------------- |
| `debug`      | ASan enabled                                  |
| `release`    | LTO enabled                                   |
| `releasedbg` | Optimized + debug info                        |
| `coverage`   | `-fprofile-instr-generate -fcoverage-mapping` |

## Code Conventions (strict — clang-format enforces layout)

- **East const:** `std::string const&`, `fs::path const&`
- **Trailing return:** `auto fn(params) -> ReturnType` on ALL functions, including `main`
- **Files:** `snake_case` (e.g., `video_info.h`, `job_state_tests.cpp`)
- **Types:** `PascalCase` (e.g., `ConfigSnapshot`, `PackService`)
- **Functions/methods:** `camelCase` (e.g., `mergeTasks()`, `markRunning()`)
- **Members:** `camelCase` + trailing `_` (e.g., `stateFilePath_`, `mtx_`)
- **Constants:** `k` + `PascalCase` (e.g., `kEncodeVideoKind`, `kFlushIntervalMs`)
- **Namespaces:** lowercase, no separators (e.g., `jobstate`, `appentry`, `videobatch`), no indent inside
- **Header guards:** `#pragma once` ONLY — never `#ifndef` guards
- **Include order:** own header first, then project headers grouped by module, then third-party, then stdlib. Paths relative to `src/` with forward slashes.
- **Comments:** Minimal. No Doxygen/JSDoc. Code is self-documenting.
- **Template params:** `Ty` (single), `Tys` (parameter pack)

## Architecture

```
main → appentry::run → pipeline::run
         ├─ runVideo() → video_process → video_batch_execution → video_encode_runner
         ├─ runPicture() → picture_process → picture_compress
         └─ runPackOnly() → pack::execute()

src/
  app/      CLI entry, pipeline orchestration
  cmd/      CLI11 command-line parsing, config builder
  core/     AppContext, JobState, TaskExecutor, Progress, MediaScan, error_handle
  infra/    Crash handler, terminal, stacktrace, stop_signal, toolchain discovery
  pack/     ZIP creation (PackRequest → PackPlan → Packer → libzippp)
  picture/  Image compression + packaging (ffmpeg WebP)
  video/    Video encode orchestration, progress parsing, output planning
  utils/    Subprocess execution (exec2), FFmpeg discovery, UUID generation
```

- **`appctx::AppContext`** is the single mutable context struct — passed by mutable reference through the entire call chain.
- **`eh::Result<T>`** = `std::expected<T, std::string>`. All operational failures return this. Exceptions only for catastrophic errors (caught in `main.cpp`).
- **`jobstate::Store`** persists task records as JSON for resume after interruption.

## Error Handling

```cpp
// Create an error:
return eh::makeError("Failed to open state file: {}", path.string());

// Check results:
if (!result) { return eh::makeError("context: {}", result.error()); }
REQUIRE(result);  // in tests
```

## Testing

- **Framework:** Catch2 v3 (`catch2/catch_all.hpp`). Custom runner in `tests/test_main.cpp`.
- **Fixtures:** `TempDir` (RAII temp dir in `test_utils.h`), `ScopedStopSignalReset`, `ScopedEnvVar`.
- **Helpers:** `writeFile()`, `touchFile()`, `listRegularFiles()`, `listZipRegularEntryNames()` in `test_utils.h`.
- **E2E fake toolchain:** `fake_media_tool.cpp` impersonates ffmpeg/ffprobe. Controlled via env vars (`ENCRO_FAKE_FFMPEG_EXIT_CODE`, etc.).
- **Real-ffmpeg tests** tagged `[real-ffmpeg]` or `[smoke]` — auto-skip via `SKIP()` if ffmpeg not on PATH.
- **Compile-only tests:** `pack_api_standalone_compile_test.cpp`, `packer_standalone_compile_test.cpp` — `static_assert` to verify API boundaries. Compilation IS the test.
- **Tag conventions:** `[job-state]`, `[cmd]`, `[pack-service]`, `[packer]`, `[video-info]`, `[e2e]`, etc.

## Development Workflow (TDD)

All new feature development must follow TDD (Test-Driven Development):

1. **Write the test first, ensure it fails (RED)** — Before writing any implementation code, write one or more test cases and verify they fail (either compilation failure or runtime failure).
2. **Write minimal code to pass the test (GREEN)** — Write only the minimum implementation needed to make the tests pass. Do not add any logic not covered by tests.
3. **Refactor (REFACTOR)** — After all tests pass, refactor under the protection of tests: eliminate duplication, improve design.
4. **Verify** — Ensure all tests still pass after refactoring.

Constraints:

- Never write implementation code first and then add tests afterwards.
- Every new feature must have at least one corresponding test case.
- Commits should typically include both test files and implementation files in the same commit.

## Key Dependencies

- **CLI11** — CLI argument parsing (migrated from Boost.ProgramOptions in v1.6)
- **Boost** — json, process, stacktrace, uuid, filesystem, program_options (legacy)
- **spdlog** — logging (async thread pool, verbose logs to `%LOCALAPPDATA%/encro/logs/`)
- **fmt** — string formatting
- **libzippp** — ZIP archive I/O
- **immer** — persistent/immutable data structures for thread-safe shared state
- **indicators** — terminal progress bars
- **thread-pool** — parallel task execution

## Platform

- **Primary:** Windows with clang-cl. Defines: `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `_MSVC_STL_HARDENING=1`.
- **Cross-platform support exists** via POSIX code paths in `src/utils/utils.cpp`.
- **External tool dependency:** ffmpeg + ffprobe (user-installed, not bundled). Discovered via PATH or `--ffmpeg-path`.

## Git

- **Pre-commit hook:** clang-format on staged C/C++ files (`.githooks/pre-commit`). Setup: `git config core.hooksPath .githooks`.
- **clang-format config** at `D:/clangformat/.clang-format` (external path, not in repo). Both pre-commit hook and `xmake format` reference it.
