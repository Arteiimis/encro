---
last_mapped_commit: 45d19cdfd54971db03ee0758ccaad6c61aad4ff1
---

# External Integrations

**Analysis Date:** 2026-05-22

## APIs & External Services

**None detected.** This is a standalone CLI application with no network integrations. No HTTP clients, no API calls, no cloud service SDKs. The application operates entirely offline on the local filesystem.

## External Tool Dependencies

**Media Processing:**
- FFmpeg - Video encoding and image compression. User-installed, not bundled. Discovered via PATH or `--ffmpeg-path` CLI argument. Invoked as a subprocess via `boost::process::v1::child` in `src/utils/utils.cpp` (`exec2Impl` function). Path resolution in `src/utils/utils.cpp` (`findFFmpeg`, lines 344-363) and `src/infra/toolchain.cpp` (`toolchain::resolve`, lines 9-31).
- FFprobe - Video metadata extraction (codec, resolution, duration, frame count). Same discovery mechanism as FFmpeg. Path resolution in `src/utils/utils.cpp` (`findFFprobe`, lines 323-342).
  - Error when not found: `"FFprobe not found. Please ensure FFprobe is installed and accessible."`

**Development Tools:**
- clang-format - Code formatting. Config at external path `D:/clangformat/.clang-format` (not in repo). Invoked by `xmake format` plugin (`plugins/format/xmake.lua`) and `.githooks/pre-commit`.
- llvm-profdata + llvm-cov - Code coverage report generation. Invoked by `xmake coverage` plugin (`plugins/coverge/xmake.lua`). Requires tools on PATH.

## Data Storage

**Databases:**
- **None.** No database of any kind (SQL, NoSQL, embedded, or otherwise).

**File Storage:**
- Local filesystem only. All input and output are filesystem paths.
- Job state persisted as JSON file on disk (local path, configurable via `--state-file`). Default path: derived from input path. Written by `src/core/job_state_store.cpp` using Boost.JSON serialization of `jobstate::Snapshot` struct (defined in `src/core/job_state.h`, lines 62-70).
- Video info cache stored in-memory using `immer::atom<immer::map<fs::path, json::value>>` (thread-safe persistent map). Not persisted to disk.

**Caching:**
- In-memory video info cache only (`appctx::RuntimeContext::VideoInfoCacheStore` in `src/core/app_context.h`, lines 95-114). Uses `immer::atom<immer::map>` for lock-free concurrent reads. No disk-level caching.

## Authentication & Identity

**Auth Provider:**
- **None.** The application has no user authentication, no login, no API keys, no tokens. It is a local CLI tool operated by the filesystem user.

## Monitoring & Observability

**Error Tracking:**
- **None.** No external error tracking, crash reporting, or telemetry service. Crash handling is entirely local: catches structured exceptions on Windows (`src/infra/crash_runtime.cpp`), generates stacktraces via `boost::stacktrace` (`src/infra/stacktrace.cpp`), and writes crash dumps using `dbghelp` (Minidump).

**Logs:**
- Local file-based logging via spdlog (async, thread-pool-backed).
- Log file location (resolved in `src/app/prelude.cpp`, `resolveCommonLogDir`, lines 42-56):
  - Windows: `%LOCALAPPDATA%/encro/logs/encro.verbose.log`
  - Linux: `$XDG_STATE_HOME/encro/logs/encro.verbose.log` or `~/.local/state/encro/logs/encro.verbose.log`
  - Fallback: system temp directory `/encro/logs/encro.verbose.log`
- Logging is opt-in via `--verbose` flag. Optional stdout echo with `--verbose-echo`.
- Log pattern: configured in `src/app/prelude.cpp` (variable `kLogPattern`). Level set to `debug` when verbose, `off` otherwise.

## CI/CD & Deployment

**Hosting:**
- **Not applicable.** This is a CLI tool distributed as a compiled binary.

**CI Pipeline:**
- **None detected.** No CI configuration files (no GitHub Actions workflows, GitLab CI, Jenkins, etc.) in the repository. The `.githooks/pre-commit` script enforces formatting locally but there is no automated CI pipeline.

**Packaging:**
- xmake built-in xpack (`xmake.lua` lines 106-114): produces NSIS installer, source ZIP, source tar.xz, binary ZIP, binary tar.xz.

## Environment Configuration

**Required env vars:**
- None. The application has no hard-required environment variables. All configuration is via CLI arguments.

**Runtime env vars used (all optional):**
- `LOCALAPPDATA` (Windows) - Base path for log directory. Read via `readWindowsEnvPath` in `src/app/prelude.cpp` line 42.
- `XDG_STATE_HOME` (Linux) - Base path for log directory. Read in `src/app/prelude.cpp` line 46.
- `HOME` (Linux) - Fallback for log directory if `XDG_STATE_HOME` not set. Read in `src/app/prelude.cpp` line 51.
- `ENCRO_FAKE_FFMPEG_EXIT_CODE` (test only) - E2E fake toolchain exit code override. Used in `tests/e2e/fake_media_tool.cpp`.
- `PATH` - FFmpeg/FFprobe discovery (standard system PATH).

**Secrets location:**
- **Not applicable.** No secrets, API keys, credentials, tokens, or passwords anywhere in the application.

## Webhooks & Callbacks

**Incoming:**
- **None.** The application has no HTTP server, no webhook endpoints, no IPC listener.

**Outgoing:**
- **None.** The application makes no HTTP requests, no webhook calls, no callback invocations.

## Notable: No Network I/O

This application performs **zero network operations**. All subprocess execution is to local binaries (FFmpeg, FFprobe). There is no `#include` for any networking library (`curl`, `asio` networking, WinSock, etc.). The only I/O is local filesystem reads/writes and standard streams (stdin/stdout/stderr).

---

*Integration audit: 2026-05-22*
