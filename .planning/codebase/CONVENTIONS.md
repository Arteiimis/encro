---
focus: quality
last_mapped_commit: 919b0cea076d2821618c3febf54f72285880cd4c
mapped_at: 2026-04-26
---

# Coding Conventions

**Analysis Date:** 2026-04-26

## Language & Toolchain

- **Standard:** C++26 (`xmake.lua:6` — `set_languages("c++26")`)
- **Toolchain:** clang-cl (`xmake.lua:7`)
- **Build:** xmake with LTO enabled (`xmake.lua:3`); `plugin.compile_commands.autoupdate` for IDE support
- **Platform:** Windows primary; conditional blocks for Linux exist in build system and sources
- **Defines (Windows):** `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `_MSVC_STL_HARDENING=1` (`xmake.lua:20-22`)

## Include Guards

All header files use `#pragma once`. No `#ifndef` / `#define` guards are used anywhere in the codebase.

**Example** (`src/core/app_context.h:1`):
```cpp
#pragma once
```

## Include Order

Includes are organized in groups separated by blank lines:

1. **Own header** (corresponding `.h` for `.cpp`)
2. **Same-project headers** (e.g., `"core/job_state.h"`, `"infra/stop_signal.h"`)
3. **Third-party libraries** (`<boost/json.hpp>`, `<catch2/catch_test_macros.hpp>`)
4. **Standard library** (`<filesystem>`, `<string>`, `<vector>`)

**Example** (`src/core/job_state.cpp:1-14`):
```cpp
#include "core/job_state.h"
#include "core/job_state_detail.h"

#include "core/collision_naming.h"
#include "utils/utils.h"

#include <boost/json.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <format>
#include <numeric>
#include <utility>
```

## Naming Patterns

### Namespaces

- **Style:** Lowercase, underscore-separated or single-word
- **Examples:** `jobstate`, `appctx`, `taskexec`, `parallel`, `media`, `progress`, `crash`, `stopsignal`, `collisionnaming`, `displaytext`, `pathroots`, `pack`, `pipeline`, `prelude`, `appentry`
- **Namespace aliases** are always placed at namespace scope, not inside functions:
  ```cpp
  namespace fs = std::filesystem;
  namespace json = boost::json;
  namespace po = boost::program_options;
  ```
- **Inline namespace alias:** `namespace eh = ErrorHandle;` at the bottom of `src/core/error_handle.h:18`
- **Anonymous namespaces** for internal linkage in `.cpp` files, closed with `}  // namespace`

### Files

- **Style:** Lowercase with underscores: `job_state.cpp`, `encode_config.h`, `video_progress_parser.cpp`
- **Pattern:** `<module_name>.h` and `<module_name>.cpp` per module
- **Header-only modules:** Some utility headers are entirely `inline` with no `.cpp`:
  - `src/core/error_handle.h`
  - `src/core/collision_naming.h`
  - `src/core/display_text.h`
  - `src/core/path_roots.h`

### Functions

- **Style:** camelCase: `buildDefaultStateFilePath()`, `resolveWorkerCount()`, `markSucceeded()`, `isStopRequested()`
- **Trailing return type** used consistently: `auto functionName() -> ReturnType`
- **Free functions preferred over member functions** — many modules export standalone functions rather than classes
- **Example** (`src/core/job_state.h:131-154`):
  ```cpp
  auto buildDefaultStateFilePath(appctx::AppConfig const& config) -> fs::path;
  auto buildConfigSnapshot(appctx::AppConfig const& config) -> ConfigSnapshot;
  auto configMatches(ConfigSnapshot const& lhs, ConfigSnapshot const& rhs) -> bool;
  ```

### Structs / Types

- **Style:** PascalCase: `AppConfig`, `TaskRecord`, `EncodingState`, `ProgressContext`
- **Type aliases:** PascalCase, suffixed with descriptive name: `EncodingStatePtr`, `EncodingStateList`, `VideoInfoCacheMap`
- **Enums:** PascalCase with PascalCase members:
  ```cpp
  enum class TaskStatus { Pending, Running, Succeeded, Failed, Interrupted };
  enum class OutputLayout { Flat, Keep };
  ```

### Class Member Variables

- **Style:** camelCase with trailing underscore: `mtx_`, `manager_`, `stateFilePath_`, `snapshot_`, `lastFlushAtMs_`
- **Example** (`src/core/job_state.h:124-128`):
  ```cpp
  fs::path stateFilePath_;
  Snapshot snapshot_;
  std::unordered_map<std::string, std::size_t> taskIndex_;
  mutable std::mutex mtx_;
  std::int64_t lastFlushAtMs_ = 0;
  ```

### Constants

- **Style:** `k`-prefix PascalCase: `kEncodeVideoKind`, `kBuildArchiveKind`, `kCanceledExitCode`, `kFlushIntervalMs`, `kStateVersion`
- **Declared:** `inline constexpr` in headers or `constexpr` in anonymous namespaces

## Code Style

### Formatting

- **Tool:** clang-format (external config at `D:/clangformat/.clang-format` — not in repo)
- **Pre-commit hook:** `.githooks/pre-commit` auto-formats staged C/C++ files via clang-format
- **VS Code:** `editor.formatOnSave: true` (`.vscode/settings.json:5`)
- **xmake plugin:** `xmake format` wraps clang-format over `src/` and `tests/` (`plugins/format/xmake.lua`)

### Auto (Type Deduction)

Use of `auto` is the default for local variables and return types. `auto const` is preferred for immutable references. Explicit types used only when needed for clarity or when `auto` would obscure intent.

**Examples:**
```cpp
auto const actualWorkers = std::max<std::size_t>(1, std::min(taskCount, workerCount));
auto const configRes = cmd::buildConfig(vm);
auto pool = BS::pause_thread_pool{actualWorkers};
```

### Designated Initializers

C++20 designated initializers are used extensively for struct construction (`src/core/job_state.cpp:620-628`, `src/core/task_executor.cpp:38-44`, `tests/cmd_config_builder_tests.cpp:40-44`):

```cpp
return TaskRunResult{
  .results = std::move(results),
  .attempted = std::move(attempted),
  .attemptedCount = 0,
  .canceled = false,
};
```

### Auto-Return Functions

The codebase uses a concise style with `->` on the same line as the closing paren of `auto` for short signatures, or on a new line for longer ones:

```cpp
auto run(appctx::AppContext& ctx) -> eh::Result<int>;

auto markProgress(
  std::string_view id,
  std::optional<float> progress = std::nullopt,
  std::optional<std::uint64_t> frameCount = std::nullopt,
  std::optional<std::string_view> status = std::nullopt
) -> void;
```

### Lambda Formatting

Lambdas are indented with the capture on one line and body on next:

```cpp
pool.detach_task([&task, index] { task(index); });

return collisionnaming::shortPathHash(payload);
```

Longer lambdas use trailing return type and multi-line body:

```cpp
auto collectEntry = [&](fs::directory_entry const& entry) {
  auto ec = std::error_code{};
  if (!entry.is_regular_file(ec) || ec) { return; }
  if (extensionMatches(entry.path(), extensions)) {
    matches.emplace_back(entry.path());
  }
};
```

### std::ranges and std::string_view

- **`std::ranges`** algorithm overloads used everywhere instead of `<algorithm>` iterator pairs: `std::ranges::sort()`, `std::ranges::all_of()`, `std::ranges::contains()`
- **`std::string_view`** used for read-only string parameters over `std::string const&`
- **`std::format`** used for string construction; `<format>` header included

## Error Handling

### Result Type

`src/core/error_handle.h` defines the project-wide error handling approach:

```cpp
namespace ErrorHandle {
  template<class Ty> using Result = std::expected<Ty, std::string>;
  // Eh::makeError(format_str, args...) -> std::unexpected<std::string>
}
namespace eh = ErrorHandle;
```

**Pattern:** Functions that can fail return `eh::Result<T>`. Success returns `T{}`. Failure returns `eh::makeError("message with {}", arg)`.

**Usage** (`src/core/task_executor.cpp:72-79`):
```cpp
try {
  results[taskIndex] = task.run(taskCtx);
} catch (std::exception const& ex) {
  results[taskIndex] =
    makeTaskError(std::format("Task {} threw exception: {}", task.id, ex.what()));
} catch (...) {
  results[taskIndex] =
    makeTaskError(std::format("Task {} threw unknown exception", task.id));
}
```

### Error Propagation in Main

`src/main.cpp` catches `std::exception` and unknown exceptions as last-resort handlers, calling crash runtime reporting.

### Validate Pattern

Some structs include a `validate()` method returning `eh::Result<void>` (`src/video/encode_config.h:23-44`):

```cpp
eh::Result<void> validate() const {
  if (!inputPath.has_value()) { return eh::makeError("Input path is required."); }
  if (!fs::exists(inputPath.value())) {
    return eh::makeError("Input path does not exist: {}", inputPath->string());
  }
  // ... more checks ...
  return {};
}
```

## Module Design

### Header / Implementation Separation

Most modules follow a strict `.h` / `.cpp` split:
- `src/video/video_info.h` + `src/video/video_info.cpp`
- `src/core/job_state.h` + `src/core/job_state.cpp`
- `src/infra/toolchain.h` + `src/infra/toolchain.cpp`

### Detail Headers

Some modules split public API from internal implementation:

- `src/core/job_state.h` — public interface
- `src/core/job_state_detail.h` — internal helpers shared between `job_state.cpp` and `job_state_store.cpp`

The detail header is included only by `.cpp` files that need it, never exposed publicly.

### Directory Structure Mapping

```
src/
├── main.cpp              # Entry point
├── app/                  # Application orchestration (prelude, pipeline, app_entry)
├── cmd/                  # CLI parsing (cmd.h/cmd.cpp, config_builder.h/config_builder.cpp)
├── core/                 # Domain logic (app_context, job_state, task_executor, media_scanner, progress, etc.)
├── infra/                # Infrastructure (terminal, toolchain, stop_signal, stacktrace, crash_runtime, console_width)
├── pack/                 # Zip packing
├── picture/              # Picture processing
├── utils/                # Utilities (exec2, readUserIpt, findFFprobe, findFFmpeg, getUUID, getParamStr)
└── video/                # Video encoding (encode_config, video_process, video_info, batch_execution, output_planning, progress_parser, encode_runner)
```

## Comments

- `//` for all comments (single-line, end-of-line, and multi-line using stacked `//`)
- No `/* */` block comments observed
- Comments are sparse — code is expected to be self-documenting through clear naming
- `//` at end of namespace closing braces: `}  // namespace jobstate`

## Function Design

**Parameters:**
- `std::string_view` for read-only strings
- `T const&` for const reference parameters
- `std::span<T const>` for array/slice parameters
- `std::optional<T>` for optional values
- Named parameters via designated initializers when many parameters needed (`TaskSpec`, `TaskPlan`, etc.)

**No raw pointers** — `std::shared_ptr`, `std::unique_ptr`, `std::optional`, and references used throughout.

## Logging

- **Framework:** spdlog with external fmt (`xmake.lua:27-28`)
- **Pattern:** `spdlog::error("{}", message)` for error logging (`src/app/app_entry.cpp:84`)
- **Also uses:** `terminal::println()` for user-facing output (color-coded)
- **Progress:** `indicators` library for progress bars (`xmake.lua:29`)

## Thread Safety

- `std::mutex` members in classes that need synchronization (`ProgressContext`, `Store`, `EncodingState`)
- `std::atomic` for lock-free counters and flags (`std::atomic_size_t`, `std::atomic<float>`)
- Thread pool: `BS::thread_pool` library (`xmake.lua:26`)

---

*Convention analysis: 2026-04-26*
