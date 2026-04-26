---
focus: tech
last_mapped_commit: 919b0cea076d2821618c3febf54f72285880cd4c
mapped_at: 2026-04-26
---

# Technology Stack

**Analysis Date:** 2026-04-26

## Languages

**Primary:**
- C++26 - Entire codebase (`src/`, `tests/`)

**Build Scripting:**
- Lua 5.x (xmake dialect) - Build system configuration (`xmake.lua`, `plugins/**/xmake.lua`)

**Shell:**
- Bash - Git pre-commit hook (`.githooks/pre-commit`)

## Runtime

**Environment:**
- Native compiled binary (no VM or interpreter)
- Compiler: clang-cl (MSVC-compatible Clang, via `set_toolchains("clang-cl")`)
- C++ standard: `c++26` with `-ftrivial-auto-var-init=pattern` hardening

**Package Manager:**
- xmake (xrepo) - Integrated package management
- Lockfile: Not present (xmake uses `xmake.lua` for dependency declarations, `.xmake/` for cache)

## Frameworks

**Core Libraries:**
| Library | Version | Purpose |
|---------|---------|---------|
| Boost | (xrepo resolved) | All sub-libraries: `program_options` (CLI), `json` (ffprobe output parsing), `process::v1` (subprocess spawning), `uuid` (unique IDs), `lexical_cast`, `parser` |
| spdlog | (xrepo resolved) | Async structured logging with file sink and optional stdout sink |
| fmt | (xrepo resolved) | String formatting (used via spdlog's `fmt_external` mode) |
| indicators | (xrepo resolved) | Terminal progress bars and spinners |
| immer | (xrepo resolved) | Persistent immutable data structures (`immer::vector`, `immer::map`, `immer::atom`) |
| libzippp | (xrepo resolved) | ZIP archive creation and manipulation |

**External Tools (Runtime Dependencies):**
- FFmpeg - Video encoding, image compression, format conversion (`src/video/encode_config.h`)
- FFprobe - Video metadata extraction via JSON (`src/video/video_info.cpp:289-295`)
- Location: Resolved from system PATH or user-specified `--ffmpeg-path` directory (`src/utils/utils.cpp:316-356`)

**Testing:**
- Catch2 - Unit/integration/E2E test framework (`tests/test_main.cpp`, `tests/e2e/e2e_test_main.cpp`)

**Build/Dev:**
- xmake - Build orchestration (`xmake.lua`)
- clang-format - Code formatting (`D:/clangformat/.clang-format`, via `plugins/format/xmake.lua` and `.githooks/pre-commit`)
- llvm-cov / llvm-profdata - Code coverage measurement (`plugins/coverge/xmake.lua`)

## Key Dependencies

**Critical (required at runtime):**
- `FFmpeg` / `FFprobe` - The tool is nonfunctional without these. Resolved at startup via `toolchain::resolve()` in `src/infra/toolchain.cpp:9-29`.

**Infrastructure (compile-time only):**
- Boost - CLI parsing (`boost::program_options`), subprocess management (`boost::process::v1`), JSON parsing (ffprobe output), UUID generation
- immer - Thread-safe persistent caches (`RuntimeContext::VideoInfoCacheStore` in `src/core/app_context.h:92-113`) and immutable collections for state management
- libzippp - All archive output

## Configuration

**Build:**
- `xmake.lua` - Primary build configuration (targets, dependencies, flags, packaging)
- Build modes: `debug`, `release`, `releasedbg`, `coverage` (LTO disabled for coverage)
- LTO enabled by default (except coverage mode)

**Runtime (CLI flags):**
- All configuration via command-line arguments parsed by `boost::program_options` (`src/app/prelude.cpp`)
- CLI definition: `src/cmd/cmd.cpp`
- Config model: `src/core/app_context.h` → `AppConfig` struct
- Config builder: `src/cmd/config_builder.cpp` → `cmd::buildConfig()`

**State Persistence:**
- Job state file: Custom JSON snapshot written to filesystem (`src/core/job_state_detail.h`, `src/core/job_state_store.cpp`)
- Default location: `<input_dir>/.encro_state.json` or fallback from `buildDefaultStateFilePath()`
- Custom location: `--state-file` flag

**Environment:**
- Windows: `%LOCALAPPDATA%` / `%APPDATA%` for log files (`src/app/prelude.cpp:40-58`)
- Linux/macOS: `$HOME/.local/state/encro/logs/`
- No `.env` files detected

## Platform Requirements

**Development:**
- xmake
- clang-cl (Windows) or Clang (cross-platform)
- FFmpeg/FFprobe on PATH
- clang-format (for formatting tasks)
- LLVM tools (for coverage)

**Production:**
- Windows (primary target, syslinks `dbghelp` for stack traces)
- Linux/macOS supported (conditional code with `dl` syslinks and POSIX signals)
- FFmpeg/FFprobe installed and on PATH or specified via `--ffmpeg-path`

---

*Stack analysis: 2026-04-26*
