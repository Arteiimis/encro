# Testing Patterns

**Analysis Date:** 2026-05-07

## Test Framework

**Runner:**
- [Catch2 v3](https://github.com/catchorg/Catch2) (via `catch2/catch_all.hpp`)
- Config: No external config file — main entry points define `CATCH_CONFIG_RUNNER` (unit tests) or `CATCH_CONFIG_MAIN` (e2e tests)

**Assertion Library:**
- Catch2 built-in macros: `REQUIRE`, `CHECK`, `REQUIRE_FALSE`, `CHECK_FALSE`
- Floating-point: `Catch::Approx()`

**Run Commands:**
```bash
xmake build tests          # Build unit + integration tests
xmake run tests            # Run unit + integration tests
xmake build e2e_tests      # Build end-to-end tests
xmake run e2e_tests        # Run end-to-end tests
xmake f -m coverage        # Switch to coverage mode
```

**Build Targets (from `xmake.lua`):**
- `tests` — unit + integration tests (links all `src/**.cpp` except `main.cpp`, plus `tests/*.cpp`)
- `e2e_tests` — end-to-end tests (depends on `encro` binary and `encro_e2e_tool` fake toolchain binary)
- `encro_e2e_tool` — fake ffmpeg/ffprobe for e2e testing

## Test File Organization

**Location:**
- Unit/integration tests: `tests/` directory, mirroring `src/` structure where possible
- E2E tests: `tests/e2e/`
- Some flat tests in `tests/` root for cross-cutting concerns

**Naming:**
- Test files: `*_tests.cpp` (e.g., `job_state_tests.cpp`, `pack_service_mock_tests.cpp`)
- Test utilities: `test_utils.h`, `tests/e2e/e2e_test_utils.h`

**Structure:**
```
tests/
├── test_main.cpp                 # Catch2 custom main with crash child support
├── test_utils.h                  # Shared test utilities (TempDir, helpers)
├── app_context_tests.cpp         # Flat tests
├── cmd_cmd_tests.cpp             # Flat tests
├── display_text_tests.cpp        # Flat tests
├── job_state_tests.cpp           # Flat tests
├── media_scanner_tests.cpp       # Flat tests
├── naming_strategy_test.cpp      # Flat tests
├── pack_api_standalone_compile_test.cpp  # Compile-time only test
├── pack_execute_test.cpp         # Flat tests
├── pack_plan_boundary_test.cpp   # Flat tests
├── pack_service_mock_tests.cpp   # Flat tests
├── pack_service_tests.cpp        # Flat tests
├── packer_standalone_compile_test.cpp   # Compile-time only test
├── packer_tests.cpp              # Flat tests
├── task_executor_tests.cpp       # Flat tests
├── utils_tests.cpp               # Flat tests
├── video_info_tests.cpp          # Flat tests
├── app/                          # App layer tests
│   ├── app_entry_tests.cpp
│   ├── pipeline_pack_only_tests.cpp
│   └── pipeline_picture_tests.cpp
├── infra/                        # Infra layer tests
│   ├── console_width_tests.cpp
│   ├── crash_runtime_tests.cpp
│   ├── progress_tests.cpp
│   ├── stacktrace_tests.cpp
│   ├── terminal_tests.cpp
│   └── toolchain_tests.cpp
├── picture/                      # Picture layer tests
│   ├── picture_compress_tests.cpp
│   └── picture_process_tests.cpp
├── video/                        # Video layer tests
│   ├── encode_config_tests.cpp
│   ├── video_batch_execution_tests.cpp
│   ├── video_output_planning_tests.cpp
│   ├── video_process_orchestration_tests.cpp
│   └── video_progress_parser_tests.cpp
└── e2e/                          # End-to-end tests
    ├── e2e_test_main.cpp         # Catch2 auto-main
    ├── e2e_test_utils.h          # Process spawning, fake toolchain
    ├── e2e_test_utils.cpp        # Process spawning implementation
    ├── encro_e2e_tests.cpp       # Full CLI tests
    └── fake_media_tool.cpp       # Fake ffmpeg/ffprobe (separate binary)
```

## Test Structure

### Suite Organization

```cpp
// Standard test file structure (from tests/job_state_tests.cpp):
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
  // helper setup function
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
  auto const task = jobstate::makeEncodeTask(inputPath, outputPath);

  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(config, false);
  REQUIRE(initRes);
  auto merged = store.mergeTasks(std::array{task});
  REQUIRE(merged.size() == 1);
  store.markRunning(task.id);
  store.markSucceeded(task.id);
  store.flush();

  // Resume
  auto resumedStore = jobstate::Store{statePath};
  auto const resumeRes = resumedStore.initialize(config, false);
  REQUIRE(resumeRes);
  CHECK(resumeRes.value());

  auto const resumed = resumedStore.mergeTasks(std::array{task});
  REQUIRE(resumed.size() == 1);
  CHECK(resumed.front().status == jobstate::TaskStatus::Succeeded);
}
```

### Assertion Patterns

- **`REQUIRE`** for hard preconditions (test cannot continue if false):
  - `REQUIRE(initRes);` — check expected/optional has value
  - `REQUIRE(result.vm.count("type") == 1);` — verify key exists
  - `REQUIRE(merged.size() == 1);` — verify collection size

- **`CHECK`** for soft assertions (test continues after failure):
  - `CHECK(resumed.front().status == jobstate::TaskStatus::Succeeded);`
  - `CHECK(result.exitCode == 0);`

- **`REQUIRE_FALSE` / `CHECK_FALSE`** for negations:
  - `REQUIRE_FALSE(result);`
  - `CHECK_FALSE(fs::exists(statePath));`

- **`Catch::Approx()`** for floating-point:
  - `CHECK(resumed.front().lastProgress.value() == Catch::Approx(100.0f));`

### Tagging Convention
- Test cases tagged with bracketed tags matching the source module:
  - `[job-state]`, `[cmd]`, `[pack-service]`, `[packer]`, `[video-info]`, `[utils]`, `[stacktrace]`
  - `[e2e]` for end-to-end, with sub-tags like `[smoke]`, `[real-ffmpeg]`, `[cli]`, `[resume]`, `[pack-only]`
  - `[groupFilesBySize]`, `[packFilesToZip]` for specific function tests

### Test Case Naming
- Descriptive English sentences starting with lowercase:
  - `"job state keeps succeeded encode action when output exists"`
  - `"commandLineInit parses multi-input values"`
  - `"encro resume skips rerunning completed encode when output exists"`

## Mocking

**Framework:** No mocking framework — all tests use real implementations or fakes

### Fake Implementations (e2e tests)
- `tests/e2e/fake_media_tool.cpp` — standalone binary that impersonates `ffmpeg` and `ffprobe`
- Detects its own executable name (`ffmpeg` or `ffprobe`) and routes accordingly
- Controlled via environment variables:
  - `ENCRO_FAKE_FFMPEG_EXIT_CODE`, `ENCRO_FAKE_FFPROBE_EXIT_CODE`
  - `ENCRO_FAKE_FFMPEG_STDERR`, `ENCRO_FAKE_FFPROBE_STDERR`
  - `ENCRO_FAKE_FFMPEG_OUTPUT_BYTES`, `ENCRO_FAKE_FFMPEG_DELAY_MS`
  - `ENCRO_FAKE_FFMPEG_PROGRESS_FRAMES`, `ENCRO_FAKE_FFPROBE_JSON_FILE`
  - `ENCRO_FAKE_TOOL_LOG_FILE` — invocation log for verifying expected calls

### What to Mock/Fake
- External processes (`ffmpeg`, `ffprobe`) — faked via `fake_media_tool.cpp`
- Global state (`stopsignal`) — reset via `ScopedStopSignalReset` RAII guard in `tests/test_utils.h`
- `std::cin` — replaced via `rdbuf()` in `tests/utils_tests.cpp`

### What NOT to Mock
- Internal module calls — always use real implementations (e.g., `pack::PackService` depends on real `pack::Packer`, not mocked)
- Filesystem — use `TempDir` for real temp directories instead of mocking `std::filesystem`

### Standalone Compile Tests
- `tests/pack_api_standalone_compile_test.cpp` and `tests/packer_standalone_compile_test.cpp`
- Use `static_assert` to verify public API boundaries at compile time
- Verify types are aggregates/enums/classes without needing runtime execution
- The compilation step IS the test

## Fixtures and Factories

### TempDir (test fixture)
Defined in `tests/test_utils.h`:
```cpp
struct TempDir {
  fs::path path;

  TempDir() {
    path = fs::temp_directory_path();
    path /= std::format(
      "video_encoder_tests_{}",
      std::chrono::steady_clock::now().time_since_epoch().count()
    );
    fs::create_directories(path);
  }

  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};
```
- RAII pattern: creates on construction, cleans up on destruction
- Used in virtually every test that touches the filesystem

### Test Helpers (testutils namespace)
Located in `tests/test_utils.h`:
```cpp
// Write a text file, creating parent directories
inline auto writeTextFile(fs::path const& filePath, std::string_view content = "x") -> void;

// Alias for writeTextFile
inline auto writeFile(fs::path const& filePath, std::string_view content = "x") -> void;

// Create empty file
inline auto touchFile(fs::path const& filePath) -> void;

// List regular files in directory, sorted
inline auto listRegularFiles(fs::path const& dirPath) -> std::vector<fs::path>;

// List regular entries (non-directory) in zip, sorted
inline auto listZipRegularEntryNames(fs::path const& zipPath) -> std::vector<std::string>;

// Strip collision-safe prefix from flat layout names
inline auto stripCollisionSafePrefix(std::string_view entryName) -> std::string_view;
```

### e2e Test Utilities
Located in `tests/e2e/e2e_test_utils.h`:
```cpp
namespace e2e {
  auto encroBinaryPath() -> fs::path;
  auto fakeMediaToolBinaryPath() -> fs::path;
  auto resolveToolOnPath(std::string_view executable) -> std::optional<fs::path>;
  auto runProcess(fs::path const& executable, std::vector<std::string> const& args, ...) -> ProcessResult;
  auto runEncro(std::vector<std::string> const& args, ...) -> ProcessResult;
  auto installFakeToolchain(fs::path const& root) -> FakeToolchain;
  auto writeTextFile(fs::path const& path, std::string_view content = "x") -> void;
  auto listZipEntries(fs::path const& zipPath) -> std::vector<std::string>;
}
```

### ScopedReset Patterns
```cpp
// Reset global stop signal state around tests
struct ScopedStopSignalReset {
  ScopedStopSignalReset() { stopsignal::reset(); }
  ~ScopedStopSignalReset() { stopsignal::reset(); }
};

// Scoped environment variable override
class ScopedEnvVar {
  ScopedEnvVar(std::string name, std::string value);
  ~ScopedEnvVar();  // restores original
};
```

## Coverage

**Requirements:** No enforced coverage threshold
**Coverage mode:** `xmake f -m coverage` enables LLVM source-based coverage:
  - Compiler flags: `-fprofile-instr-generate -fcoverage-mapping`
  - Linker flags: `-fprofile-instr-generate -fcoverage-mapping`

**View Coverage:**
```bash
xmake f -m coverage
xmake build tests
xmake run tests
# Then use llvm-cov or similar tool on the generated .profraw file
```

## Test Types

### Unit Tests
- Scope: Individual functions or small class methods
- Filesystem isolated via `TempDir`
- Examples:
  - `tests/infra/stacktrace_tests.cpp` — pure unit (just string formatting)
  - `tests/cmd_cmd_tests.cpp` — command-line parsing
  - `tests/display_text_tests.cpp` — text formatting utilities
  - `tests/packer_tests.cpp` — file grouping algorithms

### Integration Tests
- Scope: Multiple modules interacting through real implementations
- Examples:
  - `tests/job_state_tests.cpp` — tests `jobstate::Store` with real serialization
  - `tests/pack_service_mock_tests.cpp` — `PackService` with real `Packer`
  - `tests/pack_service_tests.cpp` — pack planning + execution with progress callbacks

### E2E Tests
- Scope: Full CLI execution as subprocess
- **Framework:** Catch2 (separate test binary with `CATCH_CONFIG_MAIN`)
- **Fake toolchain** for fast, deterministic testing:
  - `tests/e2e/fake_media_tool.cpp` replaces ffmpeg/ffprobe
  - Environment variables control fake behavior
- **Real ffmpeg** smoke tests for actual encoding verification:
  - Tagged `[real-ffmpeg]` or `[smoke]`
  - Skip automatically if ffmpeg/ffprobe not on PATH via `SKIP()`
  - Generate test video via ffmpeg lavfi test source
- Examples:
  - `tests/e2e/encro_e2e_tests.cpp` — full CLI e2e suite

### Compile-Only Tests
- `tests/pack_api_standalone_compile_test.cpp` and `tests/packer_standalone_compile_test.cpp`
- Use `static_assert` to verify type traits at compilation
- Verify public API boundaries without runtime

## Common Patterns

### Async Testing
Threads used in tests with `std::jthread` and `std::stop_token`:
```cpp
// From tests/utils_tests.cpp:
auto requester = std::jthread([](std::stop_token token) {
  using namespace std::chrono_literals;
  std::this_thread::sleep_for(150ms);
  if (!token.stop_requested()) { stopsignal::requestStop(); }
});

auto const result = exec2(cmd, true);
stopsignal::reset();
CHECK(result.exitCode == stopsignal::kCanceledExitCode);
```

### Error Testing
```cpp
// Expect error from operation:
REQUIRE_FALSE(result);
CHECK(result.error().find("not a directory") != std::string::npos);

// Expect error from CLI:
REQUIRE(result.exitCode == 1);
CHECK(result.stdoutText.find("Tool check failed: FFmpeg not found") != std::string::npos);
```

### Filesystem Verification
```cpp
// Verify files exist
REQUIRE(fs::exists(zipPath));

// Verify file contents / entries
auto entries = testutils::listZipRegularEntryNames(zipPath);
CHECK(entries == std::vector<std::string>{"a.txt", "b.txt", "c.txt"});

// Verify no output produced
CHECK(listRegularFiles(temp.path / "encoded_webp").empty());
```

### Test Entry Points
- **Unit/integration tests** (`tests/test_main.cpp`): Custom `main()` with `CATCH_CONFIG_RUNNER` that supports a crash child process argument for crash handler testing
- **E2E tests** (`tests/e2e/e2e_test_main.cpp`): Minimal `CATCH_CONFIG_MAIN` (2 lines)

---

*Testing analysis: 2026-05-07*
