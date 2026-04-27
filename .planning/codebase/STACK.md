# Technology Stack

**Analysis Date:** 2026-04-28

## Languages

**Primary:**
- C++26 - All application code in `src/` and `tests/`

**Secondary:**
- Lua - xmake build scripts (`xmake.lua`, `plugins/coverge/xmake.lua`, `plugins/format/xmake.lua`)
- Bash - Git hooks (`.githooks/pre-commit`)

## Runtime

**Environment:**
- Native binary (no managed runtime / virtual machine)
- Platform: Windows primary; cross-platform compatibility via conditional compilation (`src/utils/utils.cpp` has `#if defined(_WIN32)` blocks)

**Package Manager:**
- xmake built-in package manager (declared in `xmake.lua`)
- Lockfile: Not detected (xmake uses `.xmake/` cache directory)

## Frameworks

**Core:**
- C++ Standard Library (C++26) - filesystem, format, expected, chrono, threading, ranges
- Boost (all modules) - program_options (CLI parsing), json (ffprobe output parsing and state serialization), process (subprocess management), stacktrace (crash diagnostics), uuid (unique IDs), lexical_cast

**Testing:**
- Catch2 - Unit and integration test framework (`tests/test_main.cpp`, test files in `tests/`)
- Test runner defined as xmake target `"tests"` in `xmake.lua:59`

**Build/Dev:**
- xmake 0.1.5 (project version) - Build system and package manager (`xmake.lua`)
- clang-cl - Compiler toolchain (`xmake.lua:7`)
- clang-format - Code formatter, invoked via `xmake format` (configured in `plugins/format/xmake.lua`)
- LLVM tools (llvm-profdata, llvm-cov) - Code coverage instrumentation and reports (`plugins/coverge/xmake.lua`)
- xpack - Packaging/installer generation for NSIS, zip, tarxz, source archives (`xmake.lua:94-103`)

## Key Dependencies

**Critical:**
- FFmpeg (external CLI) - Video encoding engine; called as subprocess via `exec2()` in `src/video/video_encode_runner.cpp`; configured via `EncodeConfig` in `src/video/encode_config.h`
- FFprobe (external CLI) - Video metadata extraction; called as subprocess for JSON stream info in `src/video/video_info.cpp:287-319`
- Boost.Process - Platform-abstracted subprocess execution in `src/utils/utils.cpp`

**Infrastructure:**
- spdlog (with external fmt) - Structured logging across all modules; header `src/infra/crash_runtime.cpp` uses `spdlog::default_logger_raw()`
- fmt - String formatting; used for both log messages and ffmpeg command construction
- indicators - Terminal progress bars via `indicators::DynamicProgress` and `indicators::ProgressBar` in `src/core/progress.h`
- immer - Persistent/immutable maps for thread-safe video info caching (`immer::map`, `immer::atom`) in `src/core/app_context.h:93-113`
- libzippp - ZIP archive creation for output packing; used in `src/pack/packer.h` and `src/pack/pack_service.h`
- thread-pool - Concurrent task execution; used in `src/core/task_executor.h` and parallel processing in `src/core/parallel.h`

## Configuration

**Environment:**
- No `.env` files detected
- External tool paths resolved at runtime via system PATH or user-specified install directory (`src/infra/toolchain.cpp` finds ffmpeg/ffprobe)
- Runtime configuration via CLI arguments parsed by Boost.ProgramOptions (`src/cmd/cmd.h`)

**Build:**
- `xmake.lua` - Root build configuration (compiler flags, dependencies, targets, packaging)
- `.vscode/settings.json` - Editor configuration (debug target, format-on-save)
- `compile_commands.json` - Generated in `build/` directory for IDE integration (via `plugin.compile_commands.autoupdate` rule)

**Build Modes:**
- `debug`, `release`, `releasedbg`, `coverage` (defined in `xmake.lua:2`)
- Coverage mode disables LTO and adds `-fprofile-instr-generate -fcoverage-mapping` flags (`xmake.lua:13-17`)

## Platform Requirements

**Development:**
- C++26-capable compiler (clang-cl)
- xmake build tool
- Boost libraries (via xmake package manager)
- FFmpeg and FFprobe available on PATH (or via `--ffmpeg-install-dir`)
- clang-format (optional, for `xmake format`)
- LLVM tools (optional, for `xmake coverage`)

**Production:**
- Native Windows binary (linked to `dbghelp` for crash stacktrace resolution)
- Cross-platform compatible (Linux/macOS use `dl` system lib instead of `dbghelp`)
- FFmpeg and FFprobe must be installed on target system

---

*Stack analysis: 2026-04-28*
