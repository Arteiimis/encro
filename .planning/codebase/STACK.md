---
last_mapped_commit: 45d19cdfd54971db03ee0758ccaad6c61aad4ff1
---

# Technology Stack

**Analysis Date:** 2026-05-22

## Languages

**Primary:**
- C++26 (latest working draft) - Entire codebase. Configured via `set_languages("c++26")` in `xmake.lua`. Key C++26 features in use: `std::expected`, `std::format`, `std::span`, `std::stop_token`.

**Secondary:**
- Lua 5.x - xmake build system configuration (`xmake.lua`, plugin scripts in `plugins/`)

## Runtime

**Environment:**
- Native executable (no managed runtime). Compiled via Clang/LLVM toolchain.
- Compiler: `clang-cl` (LLVM C/C++ compiler in MSVC compatibility mode)
- Linker: `lld-link` (LLVM linker)

**Package Manager:**
- xmake built-in package manager (xrepo). Lockfile: not present (packages resolved dynamically from xmake-repo).

## Frameworks

**Core:**
- None. This is a standalone native CLI application with no application framework.

**CLI Parsing:**
- CLI11 - Command-line argument parsing. Migration from Boost.ProgramOptions completed in v1.6. See `src/cmd/cmd.h` for the parse result struct and `src/cmd/cmd.cpp` for CLI11 setup.

**Testing:**
- Catch2 v3 - Test framework. Custom runner in `tests/test_main.cpp`. Included via `catch2/catch_all.hpp`.

**Build/Dev:**
- xmake - Build system (2.9.x compatible). Not CMake. All build configuration in `xmake.lua` at project root.
- clang-format - Code formatting. Config file at external path `D:/clangformat/.clang-format` (not in repo).
- llvm-profdata + llvm-cov - Code coverage toolchain (LLVM source-based coverage).

## Key Dependencies

**Critical (runtime required):**
- `boost[all]` - Boost (all modules). Key modules used: `json` (JSON parsing for video info cache, state persistence), `process::v1` (subprocess execution for FFmpeg/FFprobe), `stacktrace` (crash diagnostic), `uuid` (job ID generation), `filesystem` (cross-platform path handling), `program_options` (legacy CLI parsing, being phased out). See `src/core/app_context.h`, `src/utils/utils.cpp`, `src/infra/stacktrace.cpp`.
- `fmt` 10.x+ - String formatting library. Used throughout for `std::format_string` and log messages.
- `spdlog` - Asynchronous logging (file + optional stdout echo). Configured in `src/app/prelude.cpp` with async thread pool (queue size 8192, 1 thread). Log file at `%LOCALAPPDATA%/encro/logs/encro.verbose.log` on Windows.
- `cli11` - CLI argument parsing. Replaces Boost.ProgramOptions. See `src/cmd/cmd.h`.
- `indicators` - Terminal progress bars and spinners. Used in `src/core/progress.h` via `indicators::DynamicProgress` and `indicators::ProgressBar`.
- `libzippp` (+ `libzip`) - ZIP archive creation for output packaging. Used in `src/pack/packer.cpp`. Windows requires `toolchains=clang-cl` config for libzippp and `toolchains=clang` for its libzip dependency.
- `immer` - Persistent immutable data structures (`immer::atom<immer::map>`). Used for thread-safe video info cache in `src/core/app_context.h`.

**Development (test/build only):**
- `catch2` - Test framework. Used in all test targets (`tests`, `e2e_tests`).

**Infrastructure:**
- `thread-pool` - Parallel task execution. Used in `src/core/parallel.h` and `src/core/task_executor.cpp` for parallel video encoding and file packing.

## Configuration

**Environment:**
- No `.env` files present. All configuration via CLI arguments (CLI11-parsed).
- External tool discovery: FFmpeg and FFprobe discovered via PATH or `--ffmpeg-path` argument. See `src/utils/utils.cpp` (`findFFmpeg`, `findFFprobe`) and `src/infra/toolchain.cpp`.
- Log directory resolved from `LOCALAPPDATA` (Windows), XDG `STATE_HOME` (Linux), or `TEMP` fallback. See `src/app/prelude.cpp` (`resolveCommonLogDir`).

**Build:**
- `xmake.lua` - Primary build configuration (root). Defines 4 build modes, 3 targets, packaging rules.
- `.vscode/settings.json` - VS Code editor settings (format-on-save enabled).
- clang-format config at `D:/clangformat/.clang-format` - External path, referenced by both `xmake format` and `.githooks/pre-commit`.

## Platform Requirements

**Development:**
- Clang/LLVM toolchain (clang-cl, lld-link on Windows; or clang++, lld on Linux)
- xmake 2.9+
- clang-format (for code formatting; config at `D:/clangformat/.clang-format`)
- llvm-profdata + llvm-cov (for coverage reports)
- Git with Bash shell (pre-commit hook is a bash script)

**Production:**
- Primary target: Windows x64 (with `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `_MSVC_STL_HARDENING=1` defines)
- Cross-platform: POSIX code paths exist in `src/utils/utils.cpp` for Linux/macOS
- Packaging: NSIS installer, source archives, zip/tar.xz via xmake's built-in xpack. See `xmake.lua` lines 106-114.
- User must install FFmpeg and FFprobe separately (not bundled). See external toolchain discovery in `src/infra/toolchain.cpp`.

## Build Modes

| Mode | Flags | Purpose |
|------|-------|---------|
| `debug` | ASan enabled (`build.sanitizer.address`), MD runtime | Development/debugging with address sanitizer |
| `release` | LTO enabled (`build.optimization.lto`) | Production builds |
| `releasedbg` | Optimized + debug info | Release-like debugging |
| `coverage` | `-fprofile-instr-generate -fcoverage-mapping`, LTO disabled | Source-based code coverage |

---

*Stack analysis: 2026-05-22*
