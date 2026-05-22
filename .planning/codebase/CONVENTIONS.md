---
last_mapped_commit: 45d19cdfd54971db03ee0758ccaad6c61aad4ff1
mapped_at: 2026-05-22
focus: quality
---

# Coding Conventions

**Analysis Date:** 2026-05-22

## Language and Standard

**Language:** C++26
**Compiler:** `clang-cl` (Windows, set via xmake toolchain)
**Linker:** `lld-link`

## Naming Patterns

**Files:**
- `snake_case` for all source, header, and test files.
- Examples: `video_info.h`, `job_state_tests.cpp`, `config_builder.cpp`
- Header files use `.h`, implementation files use `.cpp`.

**Types (structs, classes, enums):**
- `PascalCase`
- Examples: `ConfigSnapshot`, `PackService`, `TaskRecord`, `MessageKind`, `OutputLayout`

**Functions and methods:**
- `camelCase`
- Examples: `mergeTasks()`, `markRunning()`, `findFFmpeg()`, `runIndexedTasks()`, `isStopRequested()`

**Member variables:**
- `camelCase` + trailing underscore `_`
- Examples: `stateFilePath_`, `mtx_`, `lastFlushAtMs_`, `snapshot_`, `outputFormat`

**Constants:**
- `k` + `PascalCase`
- Examples: `kEncodeVideoKind`, `kFlushIntervalMs`, `kCrashChildArg`, `kCanceledExitCode`

**Namespaces:**
- Lowercase, no separators (no underscores or hyphens).
- Examples: `jobstate`, `appentry`, `videobatch`, `taskexec`, `stopsignal`
- Namespace braces do not cause indentation (body not indented).
- Forward declarations of classes use namespace blocks:
  ```cpp
  namespace jobstate { class Store; }
  ```
- Common shorthand alias: `namespace fs = std::filesystem;` (declared within each namespace that needs it, at namespace scope).

**Template parameters:**
- `Ty` for a single type parameter.
- `Tys` for a parameter pack.

**Aliases:**
- Short namespace aliases for frequently used error handling:
  ```cpp
  namespace eh = ErrorHandle;
  ```

## Code Style

**Formatting:**
- **Tool:** `clang-format`
- **Config location (external):** `D:/clangformat/.clang-format` (not in repo)
- **Apply:** `xmake format`
- **Check-only:** `xmake format -k check`
- **Pre-commit hook:** `.githooks/pre-commit` runs `clang-format` on staged C/C++ files against the external config file.
- Hook setup: `git config core.hooksPath .githooks`

**Key style rules enforced by clang-format:**
- **East const:** `std::string const&`, `fs::path const&` (const always on the right).
- **Trailing return type on ALL functions, including `main`:** `auto fn(params) -> ReturnType`
  ```cpp
  auto main(int argc, char* argv[]) -> int { ... }
  auto resolveWorkerCount(std::size_t taskCount, std::size_t maxConcurrency) -> std::size_t;
  ```
  For void-returning functions: `auto fn(params) -> void`

**Header guards:**
- `#pragma once` ONLY. Never use `#ifndef`/`#define` guards.

**Braces:**
- Always braces for control flow (enforced by clang-format configuration).

**Designated initializers:**
- Used extensively for constructing aggregates:
  ```cpp
  return appctx::AppConfig{
    .processType = "video",
    .outputFormat = "mp4",
    .inputPath = inputPath,
    .stateFilePath = statePath,
  };
  ```

**Anonymous namespaces:**
- Used for file-local/internal linkage (instead of `static`):
  ```cpp
  namespace {
  auto makeConfig(fs::path const& inputPath, fs::path const& statePath) -> appctx::AppConfig {
    // ...
  }
  }  // namespace
  ```
- In tests, anonymous namespaces hold helper functions that are not shared via `test_utils.h`.
- In implementation files, anonymous namespaces hold private helper functions and constants not exposed in headers.

## Import Organization

**Include order (top to bottom):**
1. Own header first (for `.cpp` files matching their `.h`)
2. Project headers, grouped by module:
   - `src/app/`  -- CLI entry, pipeline orchestration
   - `src/cmd/`  -- CLI11 command-line parsing, config builder
   - `src/core/`  -- AppContext, JobState, TaskExecutor, Progress, MediaScan
   - `src/infra/`  -- Crash handler, terminal, stacktrace, stop_signal, toolchain
   - `src/pack/`  -- ZIP creation
   - `src/picture/` -- Image compression
   - `src/video/` -- Video encode orchestration
   - `src/utils/` -- Subprocess, FFmpeg discovery, UUID
3. Third-party headers (`catch2`, `boost`, `spdlog`, `fmt`, `libzippp`, `immer`, `indicators`)
4. Standard library headers (`<filesystem>`, `<format>`, `<string>`, etc.)

**Path conventions:**
- Include paths are relative to `src/` or `tests/` directory with forward slashes.
- Example: `#include "core/error_handle.h"` (not `../core/error_handle.h`)

**Example from `src/core/task_executor.cpp`:**
```cpp
#include "core/task_executor.h"     // own header

#include "core/parallel.h"           // project: core
#include "infra/stop_signal.h"       // project: infra

#include <algorithm>                  // stdlib
#include <atomic>
#include <exception>
#include <format>
#include <optional>
```

## Error Handling

**Framework:** `eh::Result<T>` = `std::expected<T, std::string>`
Defined in `src/core/error_handle.h`:
```cpp
namespace ErrorHandle {
template<class Ty> using Result = std::expected<Ty, std::string>;

template<class... Tys>
auto makeError(std::format_string<Tys...> const fmt, Tys&&... args) {
  return std::unexpected(std::format(fmt, std::forward<Tys>(args)...));
}
}

namespace eh = ErrorHandle;
```

**Patterns:**
- All operational failures use `eh::Result<T>` return types.
- Exceptions are reserved for catastrophic/unrecoverable errors only (caught in `main.cpp`).

**Creating errors:**
```cpp
return eh::makeError("Failed to open state file: {}", path.string());
```

**Propagating errors:**
```cpp
if (!result) { return eh::makeError("context: {}", result.error()); }
```

**Checking in production code:**
```cpp
auto const outcome = someFunction();
if (!outcome) { /* handle error */ }
```

**Checking in tests:**
```cpp
REQUIRE(result);           // asserts success, fetches .value()
REQUIRE_FALSE(result);     // asserts failure
CHECK(result.error().find("expected text") != std::string::npos);
```

All `eh::Result` type must be checked before use. No `.value()` or `->` calls without prior `REQUIRE()` or `if (!result)` check.

## Logging

**Framework:** `spdlog` (async thread pool), configured in `src/app/prelude.cpp`.

**Key configuration:**
- Log pattern set via `spdlog::set_pattern(kLogPattern)`.
- Two sinks by default: file sink (`%LOCALAPPDATA%/encro/logs/`) + stdout color sink.
- Verbose echo mode: writes all log levels; non-verbose: suppresses trace/debug.

**Patterns:**
- `spdlog::error("{}", message)` for error-level messages.
- `spdlog::warn("...")` for warnings.
- `crash::reportCaughtException()` and `crash::reportUnknownException()` for crash reporting (use spdlog internally).
- In tests, `spdlog` can be captured via `ScopedDefaultLogger` (RAII fixture that temporarily replaces the default logger with a file-sink logger for verification).

**Console output:**
- The `terminal` namespace (`src/infra/terminal.h`) provides styled text output through `fmt`-based template functions: `terminal::println()`, `terminal::println()`, `terminal::eprint()`, `terminal::eprintln()`.
- These use `MessageKind` enum for semantic roles: `Error`, `Warning`, `Success`, `Info`, `Hint`, `Prompt`, `Heading`, `Usage`, `OptionGroup`, `OptionName`, etc.

## Comments

**Style:** Minimal. No Doxygen/JSDoc comments. Code is expected to be self-documenting.

**When comments are used:**
- Module/design-level comments at the top of specific files (e.g., compile-only test files explain the intent: "If this file compiles, the public API boundary is clean.")
- Section separators: `// ---` or inline separator comments like `// ── Phase 20 additions ──`
- Inline comments for non-obvious logic or platform-specific code paths.
- No `TODO`, `FIXME`, `HACK`, or `XXX` comments were found in the codebase.

## Function Design

**Size:** Functions vary from single-line accessors to moderately sized implementations. The largest implementation files are `src/cmd/cmd.cpp` (824 lines, CLI11 setup) and `src/pack/packer.cpp` (804 lines, ZIP grouping logic). Functions tend to be focused and typically fit within a single screen.

**Parameters:**
- `std::string_view` for read-only string parameters.
- `fs::path const&` for filesystem paths.
- `std::span<T const>` for array/vector views.
- `appctx::AppContext&` (mutable reference) for the single shared context struct that flows through the call chain.
- `std::optional<T>` for nullable parameters (never raw pointers for optionals).

**Return Values:**
- `eh::Result<T>` for failable operations.
- `std::optional<T>` for queries that may not find a result.
- Plain values for infallible operations.
- Trailing return type syntax on ALL functions.

## Module Design

**Exports:**
- Public API of each module exposed through the primary header (e.g., `pack/pack.h` is the single public header for the `pack` module).
- Implementation details in `*_internal.h` or `*_types.h` headers, consumed within the module and by tests.
- Clean header boundaries verified by compile-only tests (e.g., `tests/pack_api_standalone_compile_test.cpp` asserts `pack.h` compiles without including `pack_service.h`).

**Template functions:**
- Defined inline in headers (e.g., `src/infra/terminal.h` template printing functions).

**Static methods:**
- `Packer` class uses many `static` private methods for grouping logic, avoiding unnecessary state.

## Code Structure

**Module layout (`src/`):**
```
src/
  app/      -- CLI entry (`main.cpp`), pipeline orchestration, prelude (setup)
  cmd/      -- CLI11 command-line parsing, config builder
  core/     -- AppContext, JobState, TaskExecutor, Progress, MediaScan, error_handle
  infra/    -- Crash handler, terminal, stacktrace, stop_signal, toolchain discovery
  pack/     -- ZIP creation (PackRequest -> PackPlan -> Packer -> libzippp)
  picture/  -- Image compression + packaging (ffmpeg WebP)
  video/    -- Video encode orchestration, progress parsing, output planning
  utils/    -- Subprocess execution (exec2), FFmpeg discovery, UUID generation
```

**Key structural patterns:**
- `appctx::AppContext` (`src/core/app_context.h`) is the single mutable context struct passed by mutable reference through the entire call chain.
- Free functions preferred over classes where state is not needed.
- `static` class methods used for stateless utility grouping.
- Immutable data structures (`immer::atom`, `immer::map`) for thread-safe sharing in `RuntimeContext`.

## Platform Conventions

**Windows (primary):**
- Defines: `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `_MSVC_STL_HARDENING=1`
- Cross-platform support via `#if defined(_WIN32)` / `#else` blocks in `src/utils/utils.cpp` and other files.
- Preprocessor guards use `_WIN32` / `_WIN64` for Windows, plain `#else` for POSIX.

## Anti-Patterns to Avoid

### Using `#ifndef` header guards
**Do this instead:** Always use `#pragma once` at the top of every header file.

### West const
**Do this instead:** Always use east const: `std::string const&` not `const std::string&`.

### Non-trailing return type
**Do this instead:** Every function must use trailing return type syntax: `auto fn() -> ReturnType`.

### Throwing exceptions for operational errors
**Do this instead:** Return `eh::Result<T>`. Exceptions only for catastrophic failures caught at `main()`.

### Including internal module headers from outside
**Do this instead:** Include only the module's public header (e.g., `pack/pack.h`). Internal headers (`*_internal.h`, `*_types.h`) are for intra-module use and test access.

### Skipping error checks on `eh::Result`
**Do this instead:** Always check `if (!result)` or `REQUIRE(result)` before accessing `.value()`.

### Not using East const for template parameters
**Do this instead:** Use `Ty` for single type, `Tys` for parameter packs.

---

*Convention analysis: 2026-05-22*
