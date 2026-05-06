# Technology Stack

**Analysis Date:** 2026-05-07

## Languages

**Primary:**
- C++26 - Entire codebase (`src/`, `tests/`, `plugins/`)

**Build Scripting:**
- Lua 5.x - xmake build script (`xmake.lua`, `plugins/*/xmake.lua`)

**Shell Scripting:**
- Bash - Git hooks (`.githooks/pre-commit`)

## Runtime

**Environment:**
- Native binary (compiled C++ executable `encro.exe`)
- No managed runtime or VM

**Build System:**
- xmake - Cross-platform build system
- Toolchain: `clang-cl` (Clang with MSVC-compatible CLI on Windows)
- Linker: `lld-link` (LLVM linker)
- Build modes: `debug`, `release`, `releasedbg`, `coverage`

**Package Manager:**
- xmake package manager (xrepo) — `add_requires()` in `xmake.lua`
- Lockfile: Not present (xmake generates `.xmake/` cache directory)

## Frameworks

**Core:**
- Boost (all modules) - Utility libraries: `program_options` (CLI parsing), `process::v1` (subprocess management), `json` (JSON parsing/generation), `stacktrace` (crash reporting), `uuid` (unique ID generation), `filesystem` (path manipulation)
- No web framework, no GUI framework — pure CLI tool

**Testing:**
- Catch2 - Unit/Integration/E2E testing framework
  - Test runner configured via xmake target `tests` and `e2e_tests`
  - Test files: `tests/test_main.cpp` (unit), `tests/e2e/e2e_test_main.cpp` (e2e entry point)

**Build/Dev:**
- clang-format - Code formatting (pre-commit hook + `xmake format` plugin)
- llvm-cov / llvm-profdata - Code coverage (`xmake coverage` plugin, mode `coverage`)

## Key Dependencies

**Critical:**
| Dependency | Purpose | Files consuming it |
|---|---|---|
| `boost::program_options` | CLI argument parsing | `src/cmd/cmd.cpp`, `src/cmd/config_builder.cpp`, `src/utils/utils.cpp` |
| `boost::process::v1` | Subprocess execution (FFmpeg/FFprobe) | `src/utils/utils.cpp` (exec2 implementation) |
| `boost::json` | Video metadata parsing from FFprobe output | `src/video/video_info.cpp`, `src/core/app_context.h` |
| `spdlog` | Structured logging | Used pervasively across all `src/` modules |
| `fmt` | String formatting (standalone, spdlog uses external fmt) | Used pervasively |
| `libzippp` + `libzip` | ZIP archive creation for output packaging | `src/pack/packer.h`, internal Packer implementation |
| `immer` | Persistent/immutable data structures (`map`, `vector`, `atom`) | `src/video/video_process.cpp`, `src/core/app_context.h` |
| `catch2` | Test framework | All `tests/*.cpp` files |

**Infrastructure:**
| Dependency | Purpose |
|---|---|
| `indicators` | Console progress bars (`progress::ProgressContext`) |
| `thread-pool` | Thread pool for parallel task execution |
| `boost::stacktrace` | Crash stacktrace capture (fallback when `<stacktrace>` not available) |
| `boost::uuid` | UUID generation for progress temp files |
| `boost::lexical_cast` | Type conversion utilities |

## Configuration

**Environment:**
- No `.env` files used — all configuration via CLI arguments
- Runtime tool discovery: FFmpeg/FFprobe assumed on system PATH, or specified via `--ffmpeg-path` flag
- State persistence: JSON state files written to disk (default: `<input>/encro_state.json`) via `jobstate::Store`

**Build:**
- `xmake.lua` — Project root build configuration
  - Version: `0.1.5`
  - C++ standard: `c++26`
  - Windows defines: `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `_MSVC_STL_HARDENING=1`
  - Release: LTO enabled
  - Debug: Address Sanitizer enabled
  - Coverage: LLVM source-based instrumentation

**Formatting:**
- `.githooks/pre-commit` — clang-format on staged C/C++ files
- `plugins/format/xmake.lua` — `xmake format` command
- `.clang-format` located at `D:/clangformat/.clang-format` (external path, not in repo)

## Platform Requirements

**Development:**
- Clang (clang-cl) compiler with C++26 support
- xmake build system
- FFmpeg and FFprobe on PATH (or installed separately)
- Windows: `dbghelp` system library (linked for stacktrace support)
- Unix: `dl` system library (linked for dynamic loading)

**Production:**
- Windows (primary target) — MSVC-compatible runtime
- Cross-platform capable — POSIX subprocess code paths exist in `src/utils/utils.cpp`
- FFmpeg/FFprobe runtime dependency (user-installed, not bundled)

---

*Stack analysis: 2026-05-07*
