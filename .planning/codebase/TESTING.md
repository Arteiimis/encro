---
focus: quality
last_mapped_commit: 919b0cea076d2821618c3febf54f72285880cd4c
mapped_at: 2026-04-26
---

# Testing Patterns

**Analysis Date:** 2026-04-26

## Test Framework

**Runner:** Catch2 v3

- Header: `<catch2/catch_all.hpp>` or `<catch2/catch_test_macros.hpp>`
- Configured in `xmake.lua:36`: `add_requires("catch2")`
- Two test targets: `tests` (unit/integration) and `e2e_tests` (end-to-end)

**Run Commands:**
```bash
xmake build tests           # Build unit/integration tests
xmake run tests             # Run unit/integration tests
xmake build e2e_tests       # Build E2E tests
xmake run e2e_tests         # Run E2E tests
xmake coverage              # Build with coverage mode, run tests, generate llvm-cov report
xmake coverage --summary    # Coverage summary only
```

### Test Entry Points

Each test target has its own `main()` via `CATCH_CONFIG_MAIN`:

- `tests/test_main.cpp:1-2`:
  ```cpp
  #define CATCH_CONFIG_MAIN
  #include <catch2/catch_all.hpp>
  ```

- `tests/e2e/e2e_test_main.cpp:1-2` (identical pattern)

## Test File Organization

**Location:** Separate `tests/` directory mirroring `src/` structure coarsely:

```
tests/
├── test_main.cpp                    # Main entry for unit/integration suite
├── test_utils.h                     # Shared test utilities and fixtures
├── job_state_tests.cpp              # Mirror of src/core/job_state
├── task_executor_tests.cpp          # Mirror of src/core/task_executor
├── media_scanner_tests.cpp          # Mirror of src/core/media_scanner
├── packer_tests.cpp                 # Mirror of src/pack/
├── pack_service_tests.cpp
├── utils_tests.cpp                  # Mirror of src/utils/
├── app_context_tests.cpp            # Mirror of src/core/app_context
├── display_text_tests.cpp           # Mirror of src/core/display_text
├── video_info_tests.cpp             # Mirror of src/video/video_info
├── cmd_cmd_tests.cpp                # Mirror of src/cmd/cmd
├── cmd_config_builder_tests.cpp     # Mirror of src/cmd/config_builder
├── app/                             # Mirror of src/app/
│   ├── app_entry_tests.cpp
│   ├── pipeline_pack_only_tests.cpp
│   └── pipeline_picture_tests.cpp
├── infra/                           # Mirror of src/infra/
│   ├── console_width_tests.cpp
│   ├── crash_runtime_tests.cpp
│   ├── progress_tests.cpp
│   ├── stacktrace_tests.cpp
│   ├── terminal_tests.cpp
│   └── toolchain_tests.cpp
├── picture/                         # Mirror of src/picture/
│   ├── picture_compress_tests.cpp
│   └── picture_process_tests.cpp
├── video/                           # Mirror of src/video/
│   ├── encode_config_tests.cpp
│   ├── video_output_planning_tests.cpp
│   ├── video_process_orchestration_tests.cpp
│   └── video_progress_parser_tests.cpp
└── e2e/                             # End-to-end tests (separate target)
    ├── e2e_test_main.cpp
    ├── e2e_test_utils.h
    ├── e2e_test_utils.cpp
    ├── encro_e2e_tests.cpp
    └── fake_media_tool.cpp          # Helper binary for E2E tests
```

**Naming Convention:** `<module>_tests.cpp` — e.g., `job_state_tests.cpp`, `encode_config_tests.cpp`

**Building:** Test target includes `src/**.cpp` files directly (except `main.cpp`), compiling source and tests together (`xmake.lua:70-76`):
```lua
add_files("tests/*.cpp")
add_files("tests/app/*.cpp")
add_files("tests/infra/*.cpp")
add_files("tests/picture/*.cpp")
add_files("tests/video/*.cpp")
add_files("src/**.cpp|main.cpp")
```

## Test Structure

### Suite Organization

Tests use `TEST_CASE` macros with description and tags:

```cpp
TEST_CASE("description of what is tested", "[tag1][tag2]") {
  // Arrange
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  // Act
  auto const result = functionUnderTest(inputPath);

  // Assert
  REQUIRE(result.size() == 1);
  CHECK(result.front() == expected);
}
```

### Assertion Macros

**Hard preconditions (abort on failure):** `REQUIRE`, `REQUIRE_FALSE`

**Soft checks (continue on failure):** `CHECK`, `CHECK_FALSE`

```cpp
// From tests/cmd_config_builder_tests.cpp:47-64
REQUIRE(configRes);
auto const config = configRes.value();
CHECK(config.processType == "video");
CHECK(config.outputFormat == "mp4");
CHECK_FALSE(config.yesToAll);
```

**Float comparison:** `Catch::Approx` (`tests/job_state_tests.cpp:141`):
```cpp
CHECK(resumed.front().lastProgress.value() == Catch::Approx(100.0f));
```

**String matching:** `.find()` + `!= std::string::npos` for substring checks in error messages (`tests/cmd_config_builder_tests.cpp:125-127`):
```cpp
REQUIRE_FALSE(configRes);
CHECK(configRes.error().find("must be set to y or n") != std::string::npos);
```

### Expected/Result Type Assertions

The `eh::Result<T>` pattern integrates naturally with Catch2:

```cpp
auto const validation = cfg.validate();
REQUIRE(validation);          // Check .has_value() is true
REQUIRE_FALSE(validation);    // Check .has_value() is false
CHECK(validation.error().find("CRF value must be") != std::string::npos);
```

## Shared Test Utilities

### `tests/test_utils.h`

Contains shared test infrastructure:

**`TempDir` RAII fixture:**
```cpp
struct TempDir {
  fs::path path;
  TempDir() {
    path = fs::temp_directory_path();
    path /= std::format("video_encoder_tests_{}", /* timestamp */);
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};
```

**`testutils` namespace helpers:**
- `writeTextFile(path, content)` — creates file with content and parent directories
- `writeFile(path, content)` — alias for `writeTextFile`
- `touchFile(path)` — creates file with default content `"x"`
- `ScopedStopSignalReset` — RAII wrapper to reset `stopsignal` global state between tests
- `listRegularFiles(dirPath)` — sorted vector of regular file paths
- `listZipRegularEntryNames(zipPath)` — sorted list of zip entry names (excluding directories)
- `stripCollisionSafePrefix(entryName)` — extracts stem from collision-safe zip entry name
- `hasCollisionSafePrefix(entryName, dirLabel, stem)` — checks collision-safe naming pattern
- `collisionGroupPrefix(entryName)` — extracts collision group prefix

## Mocking

### Approach

The project does **not** use a mocking framework (no dependency injection framework, no mock libraries). Instead:

1. **Fake filesystem:** `TempDir` + `touchFile()` create temporary test files and directories
2. **Fake executables:** Test scripts simulate external tools (e.g., `writeFakeFfprobeScript` in `tests/video_info_tests.cpp:17-27`)
3. **Fake binary for E2E:** `tests/e2e/fake_media_tool.cpp` is a separate binary target (`encro_e2e_tool`) that simulates ffmpeg/ffprobe for end-to-end tests
4. **`std::istringstream` injection:** For testing interactive input (`tests/utils_tests.cpp:16-23`):
   ```cpp
   auto input = std::istringstream{"y\n"};
   auto* oldBuf = std::cin.rdbuf(input.rdbuf());
   auto const result = readUserIpt(false, "");
   std::cin.rdbuf(oldBuf);
   ```

### Global State Management

Global state (`stopsignal`) is reset before/after tests that depend on it:

```cpp
// tests/task_executor_tests.cpp:25
stopsignal::reset();
// ... test body ...

// Or via RAII:
// tests/app/pipeline_pack_only_tests.cpp:70
ScopedStopSignalReset stopGuard;
```

### Test Fixture Pattern

Tests that need the same setup use helper functions in anonymous namespaces:

```cpp
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
```

## Coverage

**Tool:** LLVM source-based code coverage (via `-fprofile-instr-generate -fcoverage-mapping`)

**Build config:** `xmake.lua:13-17` — coverage mode disables LTO and adds instrumentation flags:
```lua
if is_mode("coverage") then
  set_policy("build.optimization.lto", false)
  add_cxxflags("-fprofile-instr-generate", "-fcoverage-mapping")
  add_ldflags("-fprofile-instr-generate", "-fcoverage-mapping", {force = true})
end
```

**Plugin:** `plugins/coverge/xmake.lua` provides `xmake coverage`:
1. Configure with coverage mode
2. Build test binary
3. Run tests with `LLVM_PROFILE_FILE` env var
4. Merge `.profraw` files with `llvm-profdata`
5. Generate report with `llvm-cov report`

**Command:**
```bash
xmake coverage              # Full report
xmake coverage --summary    # Summary only
```

## Test Types

### Unit Tests

- **Scope:** Individual functions, structs, and small components
- **Location:** `tests/*_tests.cpp`, `tests/*/ *_tests.cpp`
- **Examples:** `tests/media_scanner_tests.cpp` (68 lines), `tests/display_text_tests.cpp` (34 lines), `tests/task_executor_tests.cpp` (118 lines)
- **Pattern:** Test one behavior per `TEST_CASE`, use `TempDir` for filesystem isolation

### Integration Tests

- **Scope:** Multi-component interactions within `app/`, `cmd/`, `video/`
- **Location:** `tests/cmd_config_builder_tests.cpp` (740 lines), `tests/video/*.cpp`, `tests/picture/*.cpp`
- **Pattern:** Build real `AppContext`, invoke pipeline functions, verify side effects on filesystem
- **Example** (`tests/app/pipeline_pack_only_tests.cpp:30-44`):
  ```cpp
  auto ctx = appctx::AppContext{};
  ctx.config.packOnly = true;
  ctx.config.processType = "video";
  ctx.config.inputPath = inputDir;
  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);
  CHECK(fs::exists(inputDir / "packed" / "input_part1[1~1#1p].zip"));
  ```

### End-to-End (E2E) Tests

- **Framework:** Separate Catch2 binary target `e2e_tests`
- **Location:** `tests/e2e/`
- **Pattern:** Invoke the actual `encro` binary as a child process, verify its output and side effects
- **Helper binary:** `tests/e2e/fake_media_tool.cpp` — standalone binary that simulates ffmpeg/ffprobe
- **Environment:** Can use real system tools (`ffmpeg`, `ffprobe`) when available; falls back to fake toolchain
- **E2E utilities** (`tests/e2e/e2e_test_utils.h`):
  - `encroBinaryPath()` — locate built encro binary
  - `runEncro(args)` — invoke encro process
  - `installFakeToolchain(root)` — set up fake ffmpeg/ffprobe
  - `listZipEntries(zipPath)` — inspect output zip contents

## Common Patterns

### Async Testing

`std::atomic` + `std::this_thread::sleep_for` for concurrency tests (`tests/task_executor_tests.cpp:27-55`):
```cpp
auto active = std::atomic_size_t{0};
auto peak = std::atomic_size_t{0};
// ... capture concurrency metrics during execution ...
CHECK(peak.load(std::memory_order_acquire) <= 2);
```

### Stop Signal Testing

`stopsignal::requestStop()` + timing assertions for cancellation behavior (`tests/utils_tests.cpp:25-50`):
```cpp
stopsignal::reset();
std::jthread requester([](std::stop_token token) {
  std::this_thread::sleep_for(150ms);
  if (!token.stop_requested()) { stopsignal::requestStop(); }
});
auto const startedAt = std::chrono::steady_clock::now();
auto const result = exec2(cmd, true);
auto const elapsed = std::chrono::steady_clock::now() - startedAt;
CHECK(result.exitCode == stopsignal::kCanceledExitCode);
CHECK(elapsed < 5s);
```

### Error Testing

Validate that functions return expected errors:
```cpp
REQUIRE_FALSE(configRes);
CHECK(configRes.error().find("Invalid process type") != std::string::npos);
```

### Filesystem Output Verification

Verify files created by operations:
```cpp
REQUIRE(fs::exists(outputDir / "input_part1[1~2#2p].zip"));
libzippp::ZipArchive zip{zipPath.string()};
zip.open(libzippp::ZipArchive::ReadOnly);
CHECK(zip.getEntries().size() == 2);
zip.close();
```

### Test Tag Conventions

Tags are used for filtering and mirror module names:
- `[job-state]`, `[task-executor]`, `[media-scanner]`, `[video-info]`, `[encode-config]`
- `[packer]`, `[cmd]`, `[config]`, `[pipeline]`, `[app-context]`, `[display-text]`
- `[utils]`, `[stacktrace]`, `[video-process]`, `[ffmpeg]`
- Sub-tags for grouping: `[packer][groupFilesBySize]`, `[packer][packFilesToZip]`

## Test Count Summary

| Directory | Test Files |
|-----------|-----------|
| `tests/` (root) | 11 |
| `tests/app/` | 3 |
| `tests/infra/` | 6 |
| `tests/picture/` | 2 |
| `tests/video/` | 4 |
| `tests/e2e/` | 1 (+ 3 support files) |
| **Total** | **27 test files** |

---

*Testing analysis: 2026-04-26*
