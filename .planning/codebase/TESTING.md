# Testing Patterns

**Analysis Date:** 2026-04-28

## Test Framework

**Runner:**
- Catch2 v3
- Included via `#include <catch2/catch_all.hpp>`
- Configured as xmake dependency: `add_requires("catch2")`

**Entry points:**
- Unit/integration tests: `tests/test_main.cpp` — contains `#define CATCH_CONFIG_MAIN`
- E2E tests: `tests/e2e/e2e_test_main.cpp` — separate `#define CATCH_CONFIG_MAIN`

**Config:** No external Catch2 config file; configuration via macros in test_main files and command-line

**Run Commands:**
```bash
xmake build tests && xmake run tests              # Run all unit/integration tests
xmake run tests -- "[tag-name]"                    # Run specific test tag
xmake build e2e_tests && xmake run e2e_tests       # Run e2e tests
xmake coverage                                      # Run tests with coverage (uses coverage mode build)
xmake coverage --summary                            # Coverage summary only
```

## Test File Organization

**Location:**
- Co-located pattern: test files mirror `src/` directory structure under `tests/`
- `tests/*.cpp` — tests for top-level/core modules
- `tests/app/*.cpp` — tests for `src/app/` modules
- `tests/infra/*.cpp` — tests for `src/infra/` modules
- `tests/picture/*.cpp` — tests for `src/picture/` modules
- `tests/video/*.cpp` — tests for `src/video/` modules
- `tests/e2e/*.cpp` — end-to-end tests (separate binary target)

**Naming:**
- Pattern: `{module}_tests.cpp` (e.g., `task_executor_tests.cpp`, `job_state_tests.cpp`, `packer_tests.cpp`)
- Matches the module name from `src/` (without directory prefix for root-level tests)
- E2E tests use descriptive names: `encro_e2e_tests.cpp`

**Test file count structure:**
```
tests/
├── test_main.cpp                          # Catch2 main entry
├── test_utils.h                           # Shared test utilities
├── app_context_tests.cpp                  # Core app context tests
├── cmd_cmd_tests.cpp                      # CLI parsing tests
├── cmd_config_builder_tests.cpp           # Config builder tests
├── display_text_tests.cpp                 # Display text utilities tests
├── job_state_tests.cpp                    # Job state persistence tests
├── media_scanner_tests.cpp                # Media scanner tests
├── pack_service_tests.cpp                 # Pack service tests
├── packer_tests.cpp                       # Packer grouping/zipping tests
├── task_executor_tests.cpp                # Task executor tests
├── utils_tests.cpp                        # Utility function tests
├── video_info_tests.cpp                   # Video info tests
├── app/
│   ├── app_entry_tests.cpp
│   ├── pipeline_pack_only_tests.cpp
│   └── pipeline_picture_tests.cpp
├── infra/
│   ├── console_width_tests.cpp
│   ├── crash_runtime_tests.cpp
│   ├── progress_tests.cpp
│   ├── stacktrace_tests.cpp
│   ├── terminal_tests.cpp
│   └── toolchain_tests.cpp
├── picture/
│   ├── picture_compress_tests.cpp
│   └── picture_process_tests.cpp
├── video/
│   ├── encode_config_tests.cpp
│   ├── video_batch_execution_tests.cpp
│   ├── video_output_planning_tests.cpp
│   ├── video_process_orchestration_tests.cpp
│   └── video_progress_parser_tests.cpp
└── e2e/
    ├── e2e_test_main.cpp
    ├── e2e_test_utils.h
    ├── e2e_test_utils.cpp
    ├── encro_e2e_tests.cpp
    └── fake_media_tool.cpp
```

## Test Structure

**Suite Organization:**
```cpp
#include "module_under_test.h"
#include "test_utils.h"            // When TempDir or testutils helpers needed

#include <catch2/catch_all.hpp>

#include <standard_headers>

namespace fs = std::filesystem;
using testutils::touchFile;       // Selective using-declarations

namespace {                        // Anonymous namespace for test-local helpers

auto makeConfig(...) -> SomeType {
  // Factory function
}

}  // namespace

TEST_CASE("descriptive behavior description", "[tag1][tag2]") {
  // Arrange
  TempDir temp;
  auto const filePath = temp.path / "input.mp4";
  touchFile(filePath);

  // Act
  auto const result = functionUnderTest(...);

  // Assert
  REQUIRE(result.size() == 1);
  CHECK(result.front() == filePath);
}
```

**TestCase patterns:**
- `TEST_CASE("single line description", "[tag]")` — short descriptions
- `TEST_CASE("longer description that\nspans two lines", "[tag]")` — multi-line descriptions use embedded newline
- Test case descriptions are human-readable behavior descriptions, not function names
- Tags use lowercase-kebab: `[task-executor]`, `[job-state]`, `[video-process]`, `[packer]`, `[groupFilesBySize]`
- Tags used for categorization and filtering, typically matching module names

**Assertion patterns:**
- `REQUIRE(expr)` — critical precondition (test stops on failure)
- `REQUIRE_FALSE(expr)` — critical negated check
- `CHECK(expr)` — non-critical assertion
- `CHECK_FALSE(expr)` — negated non-critical assertion
- `CHECK(expr == expected)` — preferred over `CHECK_EQ` (not used in this codebase)
- `CHECK(longExpr.find(substr) != std::string::npos)` — string contains check
- `Catch::Approx(100.0f)` — floating point comparison: `CHECK(task.lastProgress.value() == Catch::Approx(100.0f))`
- `REQUIRE(result)` — checks `eh::Result<T>` has value (uses `operator bool`)
- `REQUIRE_FALSE(result)` — checks that `eh::Result<T>` has error
- `CHECK(result.error() == "expected error message")` — exact error message check
- `CHECK(result.error().find("substring") != std::string::npos)` — substring error check

**Setup/Teardown:**
- `TempDir` (RAII struct in `tests/test_utils.h`) — creates unique temp directory, auto-cleans in destructor:
  ```cpp
  TempDir temp;
  auto const filePath = temp.path / "sample.mp4";
  // temp.path and contents cleaned up when temp goes out of scope
  ```
- `testutils::ScopedStopSignalReset` — resets global stop signal state:
  ```cpp
  testutils::ScopedStopSignalReset resetGuard;
  ```
- `ScopedEnvVar` (test-local) — sets env var, restores original on destruction
- `ScopedCurrentPath` (test-local) — changes working directory, restores on destruction
- Anonymous namespace for factory functions that create test configs/objects

## Mocking

**Framework:** No formal mocking framework used. Manual test doubles.

**Patterns:**
1. **Global state reset:**
   ```cpp
   stopsignal::reset();  // Reset before test that modifies stop signal
   ```

2. **Fake external tools:**
   - `tests/e2e/fake_media_tool.cpp` — a standalone binary that simulates ffmpeg/ffprobe behavior
   - Windows batch scripts for orchestration tests:
     ```cpp
     void writeFakeFfmpegScript(fs::path const& scriptPath) {
       auto const script = std::format(R"(@echo off ...)", ...);
       // Write script and use it as fake ffmpeg
     }
     ```
   - `e2e::FakeToolchain` struct — paths to fake tool binaries

3. **Input redirection:**
   ```cpp
   auto input = std::istringstream{"y\n"};
   auto* oldBuf = std::cin.rdbuf(input.rdbuf());
   auto const result = readUserIpt(false, "");
   std::cin.rdbuf(oldBuf);
   ```

4. **Real filesystem operations:** Tests create real temp files and directories rather than mocking the filesystem. `testutils::writeTextFile()`, `testutils::touchFile()`, `std::ofstream` used directly.

**What to Mock:**
- Global state that persists between tests (stop signal, current working directory, env vars)
- External tool invocations (ffmpeg/ffprobe) — via fake binaries or scripts

**What NOT to Mock:**
- Filesystem operations — use `TempDir` for isolation
- Standard library or boost components
- Internal project code — tested directly with real dependencies

## Fixtures and Factories

**Shared test utilities** (`tests/test_utils.h`):
```cpp
namespace testutils {

struct ScopedStopSignalReset {
  ScopedStopSignalReset() { stopsignal::reset(); }
  ~ScopedStopSignalReset() { stopsignal::reset(); }
};

inline auto writeTextFile(fs::path const& filePath, std::string_view content = "x") -> void;
inline auto touchFile(fs::path const& filePath) -> void;
inline auto listRegularFiles(fs::path const& dirPath) -> std::vector<fs::path>;
inline auto listZipRegularEntryNames(fs::path const& zipPath) -> std::vector<std::string>;
inline auto stripCollisionSafePrefix(std::string_view entryName) -> std::string_view;
inline auto hasCollisionSafePrefix(std::string_view entryName, std::string_view dirLabel, std::string_view stem) -> bool;
inline auto collisionGroupPrefix(std::string_view entryName) -> std::string;

}  // namespace testutils
```

**Test-local factories** (anonymous namespace in test files):
```cpp
namespace {
auto makeConfig(fs::path const& inputPath, fs::path const& statePath) -> appctx::AppConfig {
  auto config = appctx::AppConfig{};
  config.processType = "video";
  config.outputFormat = "mp4";
  config.inputPath = inputPath;
  config.stateFilePath = statePath;
  return config;
}

auto createTempFile(fs::path const& dir, std::string_view name) {
  auto const filePath = dir / name;
  std::ofstream file{filePath};
  file << "dummy";
  return filePath;
}

void createSizedSparseFile(fs::path const& filePath, std::uintmax_t sizeInBytes) {
  auto out = std::ofstream{filePath, std::ios::binary};
  REQUIRE(out.is_open());
  if (sizeInBytes == 0) { out.flush(); return; }
  out.seekp(static_cast<std::streamoff>(sizeInBytes - 1));
  out.put('\0');
  out.flush();
}
}  // namespace
```

**Location:**
- Shared fixtures and helpers in `tests/test_utils.h`
- Test-specific fixtures in anonymous namespace at top of each test file
- E2E test utilities in `tests/e2e/e2e_test_utils.h`

## Coverage

**Requirements:** No enforced coverage threshold. Coverage is opt-in via xmake `coverage` mode.

**Coverage build:**
- xmake mode `coverage` (defined in `xmake.lua:2`)
- Disables LTO: `set_policy("build.optimization.lto", false)`
- Adds Clang instrumentation flags: `-fprofile-instr-generate`, `-fcoverage-mapping`
- Linker flags: `-fprofile-instr-generate`, `-fcoverage-mapping`

**Run and view:**
```bash
xmake coverage                 # Full report — builds, runs tests, merges profdata, prints table
xmake coverage --summary       # Summary-only report
```
The `plugins/coverge/xmake.lua` task:
1. Configures coverage mode build
2. Builds `tests` target
3. Runs tests with `LLVM_PROFILE_FILE` env var pointing to `build/coverage/tests-%p.profraw`
4. Merges `.profraw` files into `tests.profdata` via `llvm-profdata merge`
5. Generates report via `llvm-cov report`

## Test Types

**Unit Tests:**
- Scope: Individual functions, small classes, pure logic (no external tool calls)
- Approach: Instantiate the unit under test with controlled inputs, assert outputs
- Examples: `task_executor_tests.cpp`, `encode_config_tests.cpp`, `display_text_tests.cpp`, `stacktrace_tests.cpp`
- These tests run fast and have no external dependencies beyond filesystem temp dirs

**Integration Tests:**
- Scope: Multiple modules interacting, state persistence, pipeline stages
- Approach: Create real files, run multiple operations, verify state files
- Examples: `job_state_tests.cpp` (tests state file read/write/resume cycles), `video_output_planning_tests.cpp` (tests full output planning with real filesystem layout)
- May depend on filesystem and real temporary directories

**E2E Tests:**
- Framework: Catch2, separate `e2e_tests` xmake target
- Scope: Full process invocation, toolchain integration, real ffmpeg/ffprobe calls
- Approach: Run `encro` as a subprocess (`e2e::runEncro()`), verify exit codes and output files
- Location: `tests/e2e/encro_e2e_tests.cpp`
- Requires fake media tools (`fake_media_tool.cpp`) compiled as `encro_e2e_tool`
- Uses `e2e::ProcessResult` to capture exit code, stdout, stderr
- Some tests gated on system tool availability: `systemToolAvailable("ffmpeg")`

## Build Configuration for Tests

The `tests` target in `xmake.lua` (lines 59-76):
- Binary target, not default build
- Links against: catch2, boost, thread-pool, indicators, fmt, spdlog, libzippp, immer
- Includes: `src` and `tests` directories
- Source files: all `tests/*.cpp`, `tests/{app,infra,picture,video}/*.cpp`
- Also includes all `src/**.cpp` except `main.cpp` (so test code has access to all production implementations)
- On Windows: links `dbghelp` system library for stack trace support

The `e2e_tests` target (lines 78-92):
- Separate binary, not default build
- Dependencies on `encro` and `encro_e2e_tool` targets
- All e2e test files except `fake_media_tool.cpp`

## Common Testing Patterns

**Async/Threaded Testing:**
```cpp
TEST_CASE("runTasks executes tasks within configured concurrency", "[task-executor]") {
  stopsignal::reset();
  auto active = std::atomic_size_t{0};
  auto peak = std::atomic_size_t{0};

  auto tasks = std::vector<taskexec::TaskSpec>{};
  tasks.push_back(taskexec::TaskSpec{
    .id = "task-0",
    .label = "Task 0",
    .run = [&](taskexec::TaskContext&) -> eh::Result<void> {
      auto const current = active.fetch_add(1, std::memory_order_acq_rel) + 1;
      // Track concurrency peak...
      std::this_thread::sleep_for(20ms);
      active.fetch_sub(1, std::memory_order_acq_rel);
      return {};
    }
  });

  auto const result = taskexec::runTasks(taskexec::TaskPlan{
    .tasks = std::move(tasks), .maxConcurrency = 2
  });

  CHECK(peak.load() <= 2);  // Concurrency is capped
}
```
Key: Always reset `stopsignal` before async tests. Use atomics to observe concurrent behavior.

**Error Handling Testing:**
```cpp
TEST_CASE("runTasks preserves task failures", "[task-executor]") {
  auto tasks = std::vector<taskexec::TaskSpec>{
    taskexec::TaskSpec{
      .id = "fail",
      .label = "fail",
      .run = [](taskexec::TaskContext&) -> eh::Result<void> {
        return eh::makeError("expected failure");
      },
    },
  };
  auto const result = taskexec::runTasks(...);

  REQUIRE_FALSE(result.results[0]);
  CHECK(result.results[0].error() == "expected failure");
}
```
Pattern: Test both success and failure paths. Check `.error()` for exact messages or substrings.

**Validation Testing:**
```cpp
TEST_CASE("EncodeConfig rejects missing input", "[encode-config]") {
  EncodeConfig cfg;
  auto const validation = cfg.validate();
  REQUIRE_FALSE(validation);
  CHECK(validation.error().find("Input path is required") != std::string::npos);
}
```
Pattern: Call validation, assert failure, check error message contains expected text.

**Temp Directory Pattern:**
```cpp
TEST_CASE("test with file operations", "[some-tag]") {
  TempDir temp;
  auto const filePath = temp.path / "input.mp4";
  testutils::touchFile(filePath);

  auto const result = functionUnderTest(temp.path);

  REQUIRE(result);
  // temp.path auto-cleaned
}
```

**Multi-TestCase Orchestration (for stateful workflows):**
```cpp
// Each test case is independent but tests a step in a workflow:
TEST_CASE("job state keeps succeeded action when output exists", "[job-state]") {
  TempDir temp;
  writeFile(inputPath); writeFile(outputPath);
  // Create store, mark running, mark succeeded, flush
  // Open new store, verify status is Succeeded
}

TEST_CASE("job state resets succeeded action when output is missing", "[job-state]") {
  TempDir temp;
  writeFile(inputPath); writeFile(outputPath);
  // Create store, mark running, mark succeeded, flush
  // Delete output file
  // Open new store, verify status is Pending (reset)
}
```
Pattern: Each test case is self-contained with its own `TempDir` and setup, but they test sequential state machine transitions.

## Test Limitations

- **No mock framework:** Manual test doubles only. Real filesystem operations used.
- **Limited async testing:** Only basic concurrency verification. No stress/race condition testing.
- **Platform windows-centric:** Some tests use Windows-specific scripting (`cmd.exe`, batch files).
- **Coverage not enforced:** No minimum coverage gate in CI or build.
- **E2E tests depend on system tools:** `encro_e2e_tests.cpp` gates on system `ffmpeg` availability.

---

*Testing analysis: 2026-04-28*
