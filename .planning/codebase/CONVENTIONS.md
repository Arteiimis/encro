# Coding Conventions

**Analysis Date:** 2026-04-28

## Naming Patterns

**Files:**
- Source/header pairs: `snake_case.h` / `snake_case.cpp` (e.g., `task_executor.h`, `task_executor.cpp`)
- Test files: `*_tests.cpp` (e.g., `job_state_tests.cpp`, `packer_tests.cpp`)
- Headers use `#pragma once` exclusively; no include guards

**Functions:**
- camelCase: `runTasks`, `resolveWorkerCount`, `buildDefaultStateFilePath`, `parseTaskStatus`
- Trailing return type used almost universally: `auto funcName(params) -> ReturnType`
- `constexpr` used for compile-time constants and simple inline functions
- `inline` used for header-defined free functions

**Variables:**
- camelCase for locals and parameters: `taskIndex`, `workerCount`, `inputPath`, `maxGroupSize`
- Private member variables: trailing underscore (`stateFilePath_`, `mtx_`, `snapshot_`, `bars_`, `lastFlushAtMs_`)
- Atomic variables: `g` prefix for file-static globals (`gStopRequested`, `gInstalled`)

**Types (structs, classes, enums):**
- PascalCase: `TaskContext`, `AppConfig`, `TaskRunResult`, `EncodingState`, `Snapshot`
- Enum values: PascalCase: `Pending`, `Running`, `Succeeded`, `Failed`, `Interrupted`, `Flat`, `Keep`
- Template parameters: short CamelCase: `Ty`, `Tys`

**Constants:**
- `inline constexpr auto kXxx` pattern: `kEncodeVideoKind`, `kBuildArchiveKind`, `kFlushIntervalMs`, `kDefaultMaxArchiveGroupSize`, `kCanceledExitCode`
- `constexpr auto kXxx` for local scope: `kLogPattern`, `kFlatEntryPrefix`, `kForceExitGracePeriod`

**Namespaces:**
- Project namespaces are lower_snake_case: `taskexec`, `jobstate`, `appctx`, `stopsignal`, `displaytext`, `collisionnaming`, `pathroots`, `videobatch`, `testutils`, `appentry`, `prelude`, `terminal`, `toolchain`, `parallel`, `progress`, `media`, `pack`, `crash`, `cmd`
- Anonymous namespaces used extensively for file-local helpers (implementation details, local constants)
- `namespace fs = std::filesystem;` declared at file/top-of-namespace scope in most files
- Namespace aliases inside namespace blocks: `namespace json = boost::json;`, `namespace po = boost::program_options;`
- `using enum terminal::MessageKind;` used in some `.cpp` files for cleaner enum usage
- `using namespace std::chrono;` / `using namespace std::literals;` used within namespace blocks
- Each namespace closed with `// namespace {name}` comment

## Code Style

**Formatting:**
- clang-format via external config at `D:/clangformat/.clang-format` (not committed to repo)
- Pre-commit hook (`.githooks/pre-commit`) runs clang-format on staged C/C++ files
- xmake `format` task (`plugins/format/xmake.lua`) for manual formatting via `xmake format`
- VSCode configured for `formatOnSave`
- No `.clang-format` or `.clang-tidy` file in the repo

**Key style patterns observed:**
- Trailing return type for all function declarations and definitions
- Designated initializers (C++20) used heavily for struct construction:
  ```cpp
  TaskRunResult{.results = ..., .attempted = ..., .attemptedCount = 0, .canceled = false}
  ```
- Braces on same line for control flow: `if (cond) { ... }`
- `auto const` preferred over `const auto`
- `static_cast` preferred over C-style casts
- `std::size_t{0}` initialization form used consistently
- Lambda captures use `[&]` for full-reference or `[&, index]` for mixed capture

**Linting:**
- No linting tools configured in the repo (no `.clang-tidy`, no cppcheck config)
- Compiler warnings not explicitly shown in xmake.lua beyond standard flags

## Import Organization

**Order:**
1. Corresponding header (for `.cpp` files): `#include "core/task_executor.h"`
2. Other project headers: `#include "core/parallel.h"`, `#include "infra/stop_signal.h"`
3. Third-party library headers: `#include <boost/json.hpp>`, `#include <spdlog/spdlog.h>`, `#include <catch2/catch_all.hpp>`
4. Standard library headers: `#include <algorithm>`, `#include <filesystem>`, `#include <vector>`

**Patterns:**
- Project headers use `#include "path/to/file.h"` (quotes, relative to `src/` or `tests/`)
- External libraries use `#include <package/header.hpp>` (angle brackets)
- No path aliases or module imports used
- Each file includes only what it directly uses (no umbrella headers)

## Error Handling

**Primary pattern — `eh::Result<T>`:**
- Defined in `src/core/error_handle.h`:
  ```cpp
  template<class Ty> using Result = std::expected<Ty, std::string>;
  ```
- Error creation via `eh::makeError(fmt, args...)` which wraps `std::format` + `std::unexpected`:
  ```cpp
  return eh::makeError("Failed to open state file: {}", path.string());
  ```
- Success returns use `return {};` or `return value;`

**Namespace alias:**
- `namespace eh = ErrorHandle;` declared in `error_handle.h` for convenient usage across the codebase

**Exception handling:**
- Exceptions caught at boundaries only:
  - `main()` catches `std::exception` and `...` and routes to crash handler
  - `taskexec::runTasks` catches exceptions from individual task lambdas and converts to `eh::Result` errors
- `throw` used only for programmer errors (missing required fields) in `encode_config.h:49,71,86`
- Crash runtime (`src/infra/crash_runtime.cpp`) installs handlers for uncaught exceptions

**Result checking pattern:**
```cpp
auto const result = someFunction();
if (!result) {
  spdlog::error("Operation failed: {}", result.error());
  return eh::makeError("...");
}
auto const value = result.value();
```
Or in tests:
```cpp
REQUIRE(result);
CHECK_FALSE(result);
CHECK(result.error() == "expected failure");
```

## Logging

**Framework:** spdlog (async) via `#include <spdlog/spdlog.h>`

**Setup** (in `src/app/prelude.cpp`):
- Log pattern: `[%Y-%m-%dT%H:%M:%S.%e%z] [%^%l%$] %v`
- Log directory: `%LOCALAPPDATA%/encro/logs/` (Windows) or `~/.local/state/encro/logs/` (Linux)
- Log file: `encro.verbose.log`
- Async logger with single worker thread, blocking overflow policy
- `spdlog::level::off` when verbose mode not enabled
- `spdlog::level::debug` when verbose
- Flush on error level

**Patterns:**
- `spdlog::info("message with {} args", value)` — informational events (scanning, completion)
- `spdlog::debug("message")` — detailed debugging (command construction, file skips)
- `spdlog::warn("message")` — recoverable issues (fallback paths, skip decisions)
- `spdlog::error("message")` — failures (pipeline errors, tool failures)
- `spdlog::critical("message")` — crash runtime only
- No custom logger instances; all code uses `spdlog::info()`, etc. (default logger)

**Terminal output:**
- `terminal::println(MessageKind, fmt, args...)` for user-facing output
- `MessageKind` enum values: `Plain`, `Error`, `Warning`, `Success`, `Info`, `Hint`, `Prompt`, `Heading`

## Comments

**When to Comment:**
- Minimal comments in production code; code is self-documenting
- `// namespace {name}` at closing braces of namespaces
- Test files occasionally have GREEN-phase refactoring comments (`// GREEN phase: extraction complete`)
- No TODO/FIXME/HACK/XXX comments found in the codebase

**JSDoc/TSDoc:**
- Not applicable (C++ project). No Doxygen comments used.

## Function Design

**Size:** Functions are generally small and focused (10-60 lines typical). The largest functions are in `video_process.cpp` and `job_state.cpp` (50-80 lines for complex orchestration).

**Parameters:**
- Pass by `const&` for complex types: `std::string_view`, `fs::path const&`, `std::span<T const>`
- Pass by value for small types and sinks: `int`, `std::size_t`, `bool`, `std::string`
- Optional parameters use `std::optional<T>` or default arguments
- `std::span` used for array/vector views in function parameters

**Return Values:**
- `eh::Result<T>` for fallible operations
- `std::optional<T>` for "may or may not exist"
- `bool` for simple success/failure (rare, used mainly in `video_encode_runner.h`)
- Direct value types for infallible operations

**Trailing return type:**
```cpp
auto buildDefaultStateFilePath(appctx::AppConfig const& config) -> fs::path;
auto runTasks(TaskPlan const& plan) -> TaskRunResult;
```
Used on all function declarations and definitions.

## Module Design

**Exports:**
- One primary class/struct per header file (with associated free functions)
- Free functions declared alongside types in headers
- `constexpr` constants and type aliases declared in headers
- No explicit export/visibility control (single binary target)

**Header-only vs split:**
- Small utility modules are header-only: `display_text.h`, `collision_naming.h`, `path_roots.h`, `error_handle.h`, `encode_config.h`
- Most modules split: `.h` for declarations, `.cpp` for implementations
- All functions in header-only files are `inline`

**Barrel Files:**
- Not used. Each consumer includes the specific header it needs.

**File organization:**
- `src/main.cpp` — entry point
- `src/app/` — application layer (entry, pipeline, prelude)
- `src/cmd/` — CLI parsing and config building
- `src/core/` — domain logic (task executor, job state, media scanner, progress, etc.)
- `src/infra/` — infrastructure (crash handling, stacktrace, stop signal, terminal, toolchain)
- `src/video/` — video processing domain
- `src/picture/` — picture processing domain
- `src/pack/` — packing/zipping domain
- `src/utils/` — general utilities

## Platform-Specific Code

- `#if defined(_WIN32)` / `#else` guards for platform differences
- Windows: Win32 API (`windows.h`, `SetConsoleCtrlHandler`, `_dupenv_s`, `_putenv_s`)
- Windows: `dbghelp` linked for stack trace support
- Linux: POSIX signals (`csignal`, `SIGINT`, `SIGTERM`)
- `NOMINMAX` and `WIN32_LEAN_AND_MEAN` defined for Windows builds
- `_MSVC_STL_HARDENING=1` for MSVC hardening checks

## C++ Standard Features Used

- **C++26** as the language standard (`set_languages("c++26")`)
- `std::expected` (C++23) — core error handling
- `std::format` (C++20) — string formatting
- Designated initializers (C++20)
- `std::span` (C++20)
- `std::ranges` algorithms
- `std::jthread` / `std::stop_token` (C++20)
- `std::atomic` with memory ordering
- `if constexpr` (C++17)
- `static thread_local` storage
- `clang-cl` toolchain on Windows

---

*Convention analysis: 2026-04-28*
