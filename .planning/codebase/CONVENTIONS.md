# Coding Conventions

**Analysis Date:** 2026-05-07

## Language & Standard

- **Language:** C++26 (`set_languages("c++26")` in `xmake.lua`)
- **Compiler:** clang-cl (Windows), with lld-link linker
- **Build system:** xmake (`xmake.lua`)

## Naming Patterns

### Files
- **snake_case** for all source and header files (e.g., `video_info.h`, `job_state.cpp`, `error_handle.h`, `crash_runtime.cpp`)
- Test files: `*_tests.cpp` (e.g., `job_state_tests.cpp`, `pack_service_mock_tests.cpp`)
- `_detail` suffix for implementation-detail headers (e.g., `job_state_detail.h`)
- `_internal` suffix for internal-only headers (e.g., `pack_internal.h`, `pack_plan_internal.h`)
- `_types` suffix for shared type headers (e.g., `pack_types.h`, `packer_types.h`)

### Directories
- **snake_case** (e.g., `src/core/`, `src/video/`, `tests/infra/`, `tests/e2e/`)

### Namespaces
- **lowercase with no separators** — run words together (e.g., `jobstate`, `appentry`, `taskexec`, `stopsignal`, `displaytext`, `collisionnaming`, `pathroots`, `videobatch`, `videoworkflow`, `consolewidth`)
- One exception: `ErrorHandle` (PascalCase) in `src/core/error_handle.h`
- Nested namespaces use `::` with no additional braces: `namespace jobstate::detail {` in `src/core/job_state_detail.h`
- Common aliases at file scope:
  - `namespace fs = std::filesystem;`
  - `namespace json = boost::json;`
  - `namespace po = boost::program_options;`
  - `namespace eh = ErrorHandle;` (defined in `src/core/error_handle.h`)
- Namespace closing comment: `}  // namespace jobstate` (two spaces before `//`)

### Classes, Structs, Types
- **PascalCase**: `ConfigSnapshot`, `TaskRecord`, `PackService`, `ProgressContext`, `CmdParseResult`, `CursorGuard`, `ScopedEnvVar`
- `final` used explicitly on leaf classes (e.g., `class PackService final` in `src/pack/pack_service.h`)

### Member Variables
- **camelCase with trailing underscore**: `stateFilePath_`, `mtx_`, `lastFlushAtMs_`, `snapshot_`, `name_`
- Public struct members use designated initializer style (no underscore): `.processType`, `.outputFormat`, `.recursive`

### Methods & Free Functions
- **camelCase**: `initialize()`, `mergeTasks()`, `markRunning()`, `buildDefaultStateFilePath()`, `makeEncodeTask()`, `primarySourcePath()`, `needsExecution()`

### Constants
- **`k` prefix + PascalCase**: `kEncodeVideoKind`, `kBuildArchiveKind`, `kCanceledExitCode`, `kFlushIntervalMs`, `kStateVersion`, `kFlatEntryPrefix`, `kDefaultMaxArchiveGroupSize`
- Usage: `inline constexpr auto kEncodeVideoKind = std::string_view{"encode_video"};` (in `src/core/job_state.h:20`)

### Enums
- **PascalCase** for both enum type and values: `TaskStatus::Pending`, `OutputLayout::Flat`, `MessageKind::Error`, `Tone::Default`, `ColorMode::Auto`

### Template Parameters
- Single type: `Ty`
- Parameter pack: `Tys`
- Used consistently across all files (see `src/core/error_handle.h`, `src/infra/terminal.h`)

## Code Style

### Formatting
- **Tool:** `clang-format` via pre-commit hook (config at `D:/clangformat/.clang-format`)
- No in-repo `.clang-format` — configuration external to repo
- Two-space indentation
- Line length: relaxed (no strict limit in codebase; CLI help capped at 120 via `src/cmd/cmd.cpp`)

### Header Guards
- **`#pragma once`** exclusively — no `#ifndef`/`#define` guards anywhere

### Braces & Whitespace
- Opening brace on same line for functions, classes, control flow
- Single-line simple functions inline in headers are common (e.g., in `src/core/display_text.h`, `src/core/collision_naming.h`)
- Namespace does NOT add an indentation level — content starts at column 0

### const Placement
- **East const** (const on the right): `std::string const&`, `fs::path const&`, `TaskRecord const&`
- Trailing return type: `auto functionName(params) -> ReturnType` is universal

### Trailing Return Types
- **All functions** use trailing return type syntax (`auto ... -> ReturnType`)
- Examples:
  - `auto stateFilePath() const -> fs::path const&;` (from `src/core/job_state.h:76`)
  - `auto makeError(std::format_string<Tys...> const fmt, Tys&&... args) { ... }` (from `src/core/error_handle.h:12`)
  - `auto run(int argc, char* argv[]) -> int;` (from `src/app/app_entry.h:9`)
- Even `main` uses trailing return: `auto main(int argc, char* argv[]) -> int` (from `src/main.cpp:6`, `tests/test_main.cpp:39`)

### Designated Initializers
- Struct initialization uses designated initializers when constructing:
```cpp
return ConfigSnapshot{
  .processType = config.processType,
  .outputFormat = config.outputFormat,
  .outputLayout = detail::outputLayoutToString(config.outputLayout),
  .packOutput = config.packOutput,
  // ...
};
```

## Import Organization

Include order pattern observed (e.g., `src/core/job_state.cpp`, `src/video/video_info.cpp`, `src/pack/packer.cpp`):

1. **Own header first** (matching `.cpp` file with quoted include)
2. **Project headers** — grouped by module, quoted paths (e.g., `"core/media_scanner.h"`, `"utils/utils.h"`)
3. **Third-party libraries** — angle brackets, grouped by library:
   - Boost headers
   - Other libraries (`spdlog`, `indicators`, `libzippp`, `catch2`)
4. **Standard library** — angle brackets, alphabetically ordered
5. Blank line separating each group

Example from `src/video/video_encode_runner.cpp`:
```cpp
#include "video/video_encode_runner.h"

#include "video/video_progress_parser.h"

#include "core/display_text.h"
#include "infra/stop_signal.h"
#include "utils/utils.h"
#include "video/encode_config.h"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <format>
```

### Path Aliases
- Project includes use paths relative to `src/` (since `add_includedirs("src", {public = true})` in `xmake.lua:58`)
- Always use forward-slash paths even on Windows

## Error Handling

### Result Type
- Custom `Result<T>` alias defined in `src/core/error_handle.h`:
  - `template<class Ty> using Result = std::expected<Ty, std::string>;`
  - Accessed as `eh::Result<T>` via namespace alias `eh = ErrorHandle`

### Error Creation
```cpp
return eh::makeError("Failed to open state file: {}", path.string());
return eh::makeError("State file root must be a JSON object: {}", path.string());
```
- Uses `std::format` formatting strings
- Error messages are descriptive plain English sentences

### Check Pattern
```cpp
auto const initRes = store.initialize(config, false);
REQUIRE(initRes);            // in tests: check has_value
if (!initRes.has_value()) {  // in production: propagate
  return eh::makeError("...");
}
```

### Exceptions
- Top-level try/catch in `src/main.cpp` catches `std::exception` and `...`:
```cpp
try {
  return appentry::run(argc, argv);
} catch (std::exception const& ex) {
  crash::reportCaughtException("unhandled exception in main", ex);
  return 1;
} catch (...) {
  crash::reportUnknownException("unhandled exception in main");
  return 1;
}
```
- `std::error_code` used for filesystem operations (never exceptions for those)

## Logging

- **Framework:** `spdlog` (header: `<spdlog/spdlog.h>`)
- Used primarily in video processing and pack modules (e.g., `src/video/video_encode_runner.cpp`, `src/pack/packer.cpp`)
- No structured logging conventions — ad-hoc `spdlog::info()`, `spdlog::error()` calls

## Comments

- **When to comment:** Minimal — code is self-documenting through naming
- **No JSDoc/TSDoc/Doxygen** formats used
- File-level comments only seen in specific test files (e.g., `tests/pack_api_standalone_compile_test.cpp` has a multi-line comment explaining the RED/GREEN phase purpose)
- Implementation intent sometimes captured in test case descriptions (Catch2 `TEST_CASE("description")` strings)

## Function Design

- **Parameters:** Complex types passed by `const&`, primitives by value
- **Return values:** Never bare `void` from tested functions — return `Result<T>` or concrete types
- **Overloads:** Used sparingly for convenience (e.g., `exec2` in `src/utils/utils.h` has 4 overloads)
- **Inline:** Many small utility functions are `inline` in headers (e.g., all of `src/core/collision_naming.h`, `src/core/display_text.h`, `src/core/path_roots.h`)

## Module Design

### Structure
- Each module: a directory under `src/` with a public header and implementation file(s)
- Public API boundary enforced through what's included:
  - e.g., `pack/pack.h` is the single public header for the pack module
  - Internal types in `_detail`, `_internal`, or `_types` headers
- Verified by standalone compile tests (e.g., `tests/pack_api_standalone_compile_test.cpp`, `tests/packer_standalone_compile_test.cpp`)

### Class Visibility
- Classes in headers expose full interface
- Private implementation details in `.cpp` files or `detail` namespaces
- Forward declarations in headers when only pointers needed (e.g., `namespace jobstate { class Store; }` in `src/core/app_context.h:19-22`)

### Shared State Protection
- Mutexes for thread-safe access: `mutable std::mutex mtx_;` pattern
- Atomic types for single values: `std::atomic<float>`
- Persistent data structures for concurrent reads: `immer::atom<>` for video info cache

---

*Convention analysis: 2026-05-07*
