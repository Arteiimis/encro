---
last_mapped_commit: 45d19cdfd54971db03ee0758ccaad6c61aad4ff1
mapped_at: 2026-05-22
focus: quality
---

# Testing Patterns

**Analysis Date:** 2026-05-22

## Test Framework

**Runner:**
- Catch2 v3 (`catch2/catch_all.hpp`)
- Custom runner in `tests/test_main.cpp` using `#define CATCH_CONFIG_RUNNER`
- The custom runner supports a `--encro-crash-child` mode for crash handler integration testing (raises a real OS exception in a child process)

**Assertion macros:**
- `REQUIRE(expr)` -- hard assertion, test stops on failure
- `CHECK(expr)` -- soft assertion, test continues
- `REQUIRE_FALSE(expr)` -- hard assertion for false value
- `REQUIRE(result)` -- asserts `eh::Result<T>` is success, then allows `.value()` access
- `REQUIRE_FALSE(result)` -- asserts `eh::Result<T>` is failure
- `CHECK_FALSE(expr)` -- soft assertion for false value

**Run Commands:**
```bash
xmake build tests && xmake run tests              # Run all unit/integration tests
xmake run tests "[tag-name]"                       # Run tests matching a Catch2 tag filter
xmake build e2e_tests && xmake run e2e_tests       # Run end-to-end tests
xmake f -m coverage && xmake build tests           # Build with coverage instrumentation
LLVM_PROFILE_FILE=build/coverage/tests-%p.profraw xmake run tests
xmake coverage                                     # Full coverage pipeline (configure->build->run->merge->report)
xmake f -m debug && xmake build tests              # Build with Address Sanitizer enabled
```

**Test binary targets in `xmake.lua`:**
- `tests` -- unit and integration tests (includes source files from `src/` except `main.cpp`)
- `e2e_tests` -- end-to-end tests (depends on `encro` + `encro_e2e_tool` binaries)

## Test File Organization

**Location:**
- Test files live in `tests/` directory at the project root.
- Subdirectories mirror source module structure:
  ```
  tests/
  ├── app/                             # Tests for src/app/
  ├── infra/                           # Tests for src/infra/
  ├── picture/                         # Tests for src/picture/
  ├── video/                           # Tests for src/video/
  ├── e2e/                             # End-to-end tests
  ├── *_tests.cpp                      # Tests for src/core/, src/cmd/, etc.
  ├── *_standalone_compile_test.cpp    # Compile-only boundary tests
  └── test_main.cpp                    # Custom Catch2 runner
  ```

**Naming:**
- Test files: `*_tests.cpp` (e.g., `job_state_tests.cpp`, `packer_tests.cpp`)
- Compile-only tests: `*_standalone_compile_test.cpp`
- Shared test utilities: `test_utils.h`
- Shared e2e utilities: `e2e/e2e_test_utils.h`, `e2e/e2e_test_utils.cpp`

**Test file to source mapping:**
| Source Header | Test File |
|---------------|-----------|
| `src/core/job_state.h` | `tests/job_state_tests.cpp` |
| `src/pack/packer.h` | `tests/packer_tests.cpp` |
| `src/pack/pack_service.h` | `tests/pack_service_tests.cpp` |
| `src/cmd/cmd.h` | `tests/cmd_cmd_tests.cpp` |
| `src/infra/terminal.h` | `tests/infra/terminal_tests.cpp` |
| `src/pack/pack.h` | `tests/pack_api_standalone_compile_test.cpp` |

## Test Structure

**Suite Organization:**
- Each `TEST_CASE` is a self-contained, independent test.
- Tests do not depend on shared mutable state between `TEST_CASE`s.
- Test isolation is achieved through `TempDir` (per-test temporary directory).

**Pattern from `tests/job_state_tests.cpp`:**
```cpp
#include "core/job_state.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <array>
#include <filesystem>

namespace fs = std::filesystem;
using testutils::writeFile;

namespace {

auto makeConfig(fs::path const& inputPath, fs::path const& statePath)
  -> appctx::AppConfig {
  auto config = appctx::AppConfig{};
  config.processType = "video";
  config.outputFormat = "mp4";
  config.inputPath = inputPath;
  config.stateFilePath = statePath;
  return config;
}

}  // namespace

TEST_CASE("job state keeps succeeded encode action when output exists", "[job-state]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const outputPath = temp.path / "input.hevc.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  writeFile(inputPath);
  writeFile(outputPath);

  auto const config = makeConfig(inputPath, statePath);
  // ... test logic ...
  REQUIRE(initRes);
  CHECK(resumed.front().status == jobstate::TaskStatus::Succeeded);
}
```

**Setup/teardown patterns:**
- `TempDir` (RAII): creates a unique temp directory on construction, recursively removes it on destruction.
- `ScopedStopSignalReset`: resets the global stop signal state on construction and destruction.
- `ScopedEnvVar`: saves/restores an environment variable across test lifetime.
- `ScopedDefaultLogger`: temporarily replaces the spdlog default logger for test verification.
- Anonymous namespace helper functions: create configs, create files, parse args -- each test file defines its own local helpers.
- No `SECTION()` usage in most test files; only used in `tests/pack_service_tests.cpp` for `[compact]` / `[full]` sub-variants.

**Assertion patterns:**
```cpp
REQUIRE(result);                          // eh::Result success check
REQUIRE(result.value().empty());          // access value after REQUIRE
REQUIRE_FALSE(result);                    // eh::Result failure check
CHECK(result.error().find("not a directory") != std::string::npos);  // error message check
CHECK(zip.getEntries().size() == 2);      // value equality
CHECK(grouped[0] == std::vector{f1, f2}); // container comparison
```

## Mocking

**Framework:** No dedicated mocking framework. The codebase uses manual test doubles.

**Patterns:**
- **Fake toolchain (`tests/e2e/fake_media_tool.cpp`):** A separate binary that impersonates `ffmpeg`/`ffprobe`. Controlled via environment variables (`ENCRO_FAKE_FFMPEG_EXIT_CODE`, etc.).
- **Fake scripts (`tests/video/video_process_orchestration_tests.cpp`):** Batch/shell scripts written to temp files that simulate ffmpeg/ffprobe behavior (writing progress files, output files).
- **Manual stubs:** Functions like `createSizedFile()`, `createFile()` create minimal test fixtures instead of mocking filesystem calls.
- **No dependency injection frameworks.** Dependencies like `PackService` are instantiated directly in tests with real implementations.

**What to mock:**
- External tool invocations (ffmpeg, ffprobe) via fake binaries or script stubs.
- Environment variables via `ScopedEnvVar`.
- Logger output via `ScopedDefaultLogger`.

**What NOT to mock:**
- Filesystem operations -- tests use `TempDir` with real file I/O.
- Core domain objects (`Packer`, `Store`, `TaskExecutor`) -- tested directly with real implementations.
- Standard library types -- used as-is.

## Fixtures and Factories

**Test Data:**

Temporary directories with real files:
```cpp
TempDir temp;
auto const inputPath = temp.path / "input.mp4";
writeFile(inputPath, "fake content");
```

File creation helpers (defined per test file in anonymous namespace):
```cpp
namespace {
auto createFile(fs::path const& dir, std::string_view name) -> fs::path {
  auto const filePath = dir / name;
  std::ofstream out{filePath, std::ios::binary};
  out << "data";
  return filePath;
}
}
```

Sized file creation:
```cpp
static auto
createSizedFile(fs::path const& dir, std::string_view name, std::size_t sizeBytes) {
  auto const filePath = dir / name;
  std::ofstream out{filePath, std::ios::binary};
  std::string content(sizeBytes, 'x');
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  return filePath;
}
```

Configuration factory:
```cpp
auto makeConfig(fs::path const& inputPath, fs::path const& statePath)
  -> appctx::AppConfig {
  auto config = appctx::AppConfig{};
  config.processType = "video";
  config.outputFormat = "mp4";
  // ...
  return config;
}
```

**Location:**
- Shared fixtures: `tests/test_utils.h` (`TempDir`, `ScopedStopSignalReset`, `writeFile()`, `touchFile()`, `listRegularFiles()`, `listZipRegularEntryNames()`)
- Test-specific fixtures: anonymous namespace in each test file
- E2E fixtures: `tests/e2e/e2e_test_utils.h` / `.cpp` (`FakeToolchain`, `ProcessResult`, `installFakeToolchain()`, `runEncro()`)

## Coverage

**Tool:** LLVM source-based coverage (`-fprofile-instr-generate`, `-fcoverage-mapping`)
**Run via xmake plugin:** `xmake coverage` (configure->build->run->merge .profraw -> llvm-cov report)
**Requirements:** No enforcement threshold; coverage data collected but not gated.

**Manual coverage run:**
```bash
xmake f -m coverage
xmake build tests
LLVM_PROFILE_FILE=build/coverage/tests-%p.profraw xmake run tests
```

## Test Types

**Unit Tests:**
- Test individual functions, classes, and small modules in isolation.
- Use `TempDir` for filesystem isolation.
- Examples: `job_state_tests.cpp`, `packer_tests.cpp`, `utils_tests.cpp`

**Integration Tests:**
- Test interactions between modules.
- E.g., `pack_service_tests.cpp` tests `PackService` which orchestrates `Packer` + filesystem I/O + `libzippp`.
- `video_process_orchestration_tests.cpp` tests the video processing pipeline with fake ffmpeg scripts.

**End-to-End Tests (`tests/e2e/`):**
- Run the actual `encro` binary as a subprocess with controlled inputs.
- Use `encro_e2e_tool` (fake ffmpeg/ffprobe) for predictable toolchain behavior.
- Some tests use real ffmpeg/ffprobe (tagged `[real-ffmpeg]` or `[smoke]`) and auto-skip if the tools are not available on PATH:
  ```cpp
  auto requireRealToolchainOrSkip() -> void {
    if (systemToolAvailable("ffmpeg") && systemToolAvailable("ffprobe")) { return; }
    SKIP("System FFmpeg/FFprobe not available on PATH.");
  }
  ```

**Compile-Only Tests:**
- Files that contain `static_assert` and type trait checks to verify API boundaries.
- The compilation step IS the test -- if the file compiles, the test passes.
- Used to verify public API boundaries (e.g., `pack_api_standalone_compile_test.cpp` verifies `pack.h` does not leak internal types).
- Used to verify circular dependency breaks (e.g., `packer_standalone_compile_test.cpp` verifies `packer.h` compiles without `pack_service.h`).
- No runtime `TEST_CASE` needed; `static_assert` with descriptive messages serve as the assertions.

**Tag Conventions:**
- Each `TEST_CASE` includes at least one tag in square brackets.
- Module tags: `[job-state]`, `[cmd]`, `[pack-service]`, `[packer]`, `[packer][groupPackFiles]`, `[video-info]`, `[video-process]`, `[e2e]`, `[crash]`, `[terminal]`, `[toolchain]`, `[pack-service]`
- Feature tags: `[groupFilesBySize]`, `[groupPackFilesWithSubparts]`, `[packFilesToZip]`, `[readLastNLines]`, `[parseProgressFile]`
- Special tags: `[real-ffmpeg]`, `[smoke]` -- for tests requiring real external tools, auto-skipped via `SKIP()`
- Integration tag: `[integration]` -- used on crash handler test that spawns a real child process

**Tag run examples:**
```bash
xmake run tests "[job-state]"        # Run only job state tests
xmake run tests "[packer]"           # Run only packer tests
xmake run tests "[real-ffmpeg]"      # Run only real-ffmpeg-dependent tests
```

## Common Patterns

**Async/Thread Safety Testing:**
- `tests/task_executor_tests.cpp` tests parallel task execution with concurrency.
- Atomic variables inspected after threaded operations complete.
- Thread-safety of `RuntimeContext` verified via `immer::atom` immutable data structures (no explicit lock contention tests).

**Error Path Testing:**
```cpp
TEST_CASE("packAllFilesInDirectory returns error for non-existent directory", "[pack-service]") {
  TempDir temp;
  auto const nonExistentDir = temp.path / "does_not_exist";
  auto const outDir = temp.path / "out";

  pack::PackService service;
  auto result = service.packAllFilesInDirectory(nonExistentDir, outDir, ...);

  REQUIRE_FALSE(result);
  CHECK(result.error().find("not a directory") != std::string::npos);
}
```

**Boundary Condition Testing:**
```cpp
// Empty input
TEST_CASE("packGroups returns empty for empty plan", "[pack-service]") {
  auto const plan = pack::PackPlan{};
  auto const result = testService.packGroups(plan);
  REQUIRE(result);
  CHECK(result.value().empty());
}

// Zero tasks
TEST_CASE("readLastNLines returns empty for missing file", "[video-process][readLastNLines]") {
  TempDir temp;
  auto const missingPath = temp.path / "missing.log";
  auto const lastLines = readLastNLines(missingPath, 2);
  CHECK(lastLines.empty());
}
```

**State Transition Testing:**
```cpp
// "job state turns running actions into interrupted on resume"
// Create store, initialize, merge task, mark running, flush.
// Create new store from same state file, initialize, verify status is Interrupted.
```

## Test Count Summary

- Total `TEST_CASE` instances: ~290 across 32 test files.
- Tests are distributed approximately evenly across modules, with the highest counts in `cmd_config_builder_tests.cpp` (38), `cmd_cmd_tests.cpp` (28), and `video/video_output_planning_tests.cpp` (19).
- No `TEMPLATE_TEST_CASE`, `GENERATE()`, or `BENCHMARK` usage detected -- all tests are standard `TEST_CASE` macros.
- No `SECTION` nesting beyond single-level in `pack_service_tests.cpp`.

## Development Workflow

**TDD mandated:** All new features follow the RED-GREEN-REFACTOR cycle.
1. Write the test first, ensure it fails (RED).
2. Write minimal code to pass the test (GREEN).
3. Refactor under test protection (REFACTOR).
4. Verify all tests pass.

**Constraints:**
- Never write implementation code first and add tests afterwards.
- Every new feature must have at least one corresponding test case.
- Commits should typically include both test files and implementation files in the same commit.

---

*Testing analysis: 2026-05-22*
