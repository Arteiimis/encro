---
focus: tech
last_mapped_commit: 919b0cea076d2821618c3febf54f72285880cd4c
mapped_at: 2026-04-26
---

# External Integrations

**Analysis Date:** 2026-04-26

## APIs & External Services

**No external APIs or network services detected.**

This is a fully offline, local tool. There are no HTTP clients, no SDK imports for cloud services, no remote API calls, and no network dependencies.

## Data Storage

**Databases:**
- None. No database client or ORM detected.

**File-Based State:**
- Job state persisted as a custom JSON snapshot file on the local filesystem
  - Format: Custom `Snapshot` struct serialized/deserialized via `src/core/job_state_detail.h`
  - Locations:
    - Default: `src/core/job_state_detail.h` → `buildFallbackStateFilePath()` (typically `<input_dir>/.encro_state.json`)
    - Custom: `--state-file` CLI flag → `src/cmd/config_builder.cpp:342-345`
  - Purpose: Resume interrupted encoding/packing jobs, track progress across runs
  - Schema versioned: `kStateVersion = 1` in `src/core/job_state_detail.h:7`

**File Storage:**
- Local filesystem only. Outputs written to:
  - Default: Same directory as input
  - Custom: `--output/-o` flag or path aliases (`input://`, `common://`, `+`, `=`) resolved in `src/cmd/config_builder.cpp:253-274`

**Caching:**
- In-memory immutable cache for ffprobe results: `RuntimeContext::VideoInfoCacheStore` in `src/core/app_context.h:92-113`
  - Uses `immer::atom<immer::map<fs::path, json::value>>` for lock-free concurrent access
- No persistent cache or external caching service

## External Tool Dependencies

**FFmpeg (`ffmpeg`):**
- **Used for:** HEVC video encoding (mp4 via `hevc_nvenc` codec), WebP conversion, image compression to JPEG
- **Invocation:** `exec2()` via `boost::process::v1` in `src/utils/utils.cpp`
- **Location resolution:** System PATH first; falls back to `--ffmpeg-path` directory recursive search (`src/utils/utils.cpp:337-356`)
- **Command construction:**
  - Video encoding: `src/video/encode_config.h:81-109` (builds ffmpeg command string from `EncodeConfig`)
  - Image compression: `src/picture/picture_compress.h:27-34`
- **Progress monitoring:** ffmpeg `-progress` pipe parsed by `src/video/video_progress_parser.cpp`

**FFprobe (`ffprobe`):**
- **Used for:** Video metadata extraction (codec info, frame counts, duration, stream details)
- **Invocation:** `exec2()` via `boost::process::v1` in `src/utils/utils.cpp`
- **Output format:** JSON (`-print_format json -show_format -show_streams`) parsed via `boost::json` in `src/video/video_info.cpp:289-318`
- **Location resolution:** System PATH first; falls back to `--ffmpeg-path` directory (`src/utils/utils.cpp:316-335`)

## Authentication & Identity

**No authentication framework or identity provider.**

The tool runs as a local CLI binary with no user accounts, no API keys, and no authentication tokens.

## Monitoring & Observability

**Error Tracking:**
- Built-in crash handler: `src/infra/crash_runtime.cpp`
  - Windows: SEH unhandled exception filter + `SetUnhandledExceptionFilter`
  - POSIX: Signal handlers for SIGABRT, SIGFPE, SIGILL, SIGSEGV
  - Output: Stacktrace captured via `src/infra/stacktrace.cpp`, written to spdlog or stderr
- No external error tracking service (Sentry, etc.)

**Logs:**
- Framework: spdlog (async logger, thread pool of 8192 entries / 1 thread)
- Setup: `src/app/prelude.cpp:60-129`
- Log file: `<LOCALAPPDATA>/encro/logs/encro.verbose.log` (Windows) or `$HOME/.local/state/encro/logs/encro.verbose.log` (Unix)
- Console output: `indicators` library for progress bars; custom terminal formatting in `src/infra/terminal.cpp`
- Log level: debug when `--verbose` flag is present; off otherwise
- Optional stderr echo: `--verbose-echo` flag writes logs to both file and stdout

## CI/CD & Deployment

**Hosting:**
- No deployment platform detected. The tool is distributed as a standalone native binary.

**Packaging:**
- xpack integration in `xmake.lua:94-103`
- Formats: `nsis` (Windows installer), `srczip`, `srctarxz`, `zip`, `tarxz`
- Author: "Artemiss"
- Description: "encro: Universal video encoder/converter/packer"

**CI Pipeline:**
- Not detected (no GitHub Actions, GitLab CI, or other CI config files found)

## Environment Configuration

**Required environment (runtime):**
- None required. All configuration via CLI flags.
- Optional: `FFmpeg`/`FFprobe` on system PATH

**Environment variables read (Windows only, for log path):**
- `%LOCALAPPDATA%` → primary log directory
- `%APPDATA%` → fallback log directory
- `%HOME%` (Unix) → `$HOME/.local/state/encro/logs/`

**Secrets location:**
- No secrets, API keys, or credentials needed or stored

## Webhooks & Callbacks

**Incoming:**
- None

**Outgoing:**
- None

## Inter-Process Communication

**Subprocess Execution:**
- `exec2()` function in `src/utils/utils.cpp` wraps `boost::process::v1::child`
- Used exclusively for invoking external tools (ffmpeg, ffprobe)
- Supports: stdout capture, line-by-line callbacks, stderr merging, graceful termination on user interrupt (Ctrl+C)
- Stop signal integration: Terminates child processes on interrupt via `stopsignal::isStopRequested()` (`src/utils/utils.cpp:131-161`)
- Platform-specific: Windows uses `PeekNamedPipe`/`CloseHandle`; Unix uses `bp::ipstream` + reader thread

---

*Integration audit: 2026-04-26*
