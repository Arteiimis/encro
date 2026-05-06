# External Integrations

**Analysis Date:** 2026-05-07

## APIs & External Services

**No external network APIs or cloud services detected.** This is a fully offline/local CLI tool.

- No HTTP requests, no REST/gRPC/GraphQL clients
- No cloud SDKs (AWS, GCP, Azure, etc.)
- No authentication services
- No telemetry or analytics endpoints

## External Tools (Subprocess Invocations)

**FFmpeg — Video/Audio encoding and transcoding:**
- Discovery: System PATH or explicit `--ffmpeg-path` directory
- Invocation: `boost::process::v1::child` via `exec2()` helper in `src/utils/utils.cpp`
- Usage:
  - Video encoding: `ffmpeg -hide_banner -nostats -loglevel error -y -i <input> -c:v hevc_nvenc -crf 20 -progress <progress_file> <output>` (`src/video/encode_config.h:81-108`)
  - WebP encoding: `ffmpeg ... -vf "scale=-2:960:..." -c:v libwebp -q:v <quality> -loop 0 <output>` (`src/video/encode_config.h:92-96`)
  - Image compression: `ffmpeg -hide_banner -nostats -loglevel error -y -i <input> -q:v <quality> <output>` (`src/picture/picture_compress.cpp:73-79`)
  - Version check: `ffmpeg -version` (`src/utils/utils.cpp:338`)
  - Stop signal handling: Process termination on user interrupt (`src/utils/utils.cpp:139-143`)

**FFprobe — Video metadata inspection:**
- Discovery: System PATH or explicit `--ffmpeg-path` directory
- Invocation: Same `exec2()` helper
- Usage:
  - JSON metadata: `ffprobe -v quiet -print_format json -show_format -show_streams <video>` (`src/video/video_info.cpp:291-295`)
  - Version check: `ffprobe -version` (`src/utils/utils.cpp:317`)
  - Output parsed via `boost::json::parse()` for stream info, codec detection, frame counts
- Key consumers: `src/video/video_info.cpp` (`getVidInfo()`, `getVidTotalFrames()`, `isHevcEncoded()`), `src/infra/toolchain.cpp`

**clang-format — Code formatting (dev tool only):**
- Invocation: Via pre-commit hook (`.githooks/pre-commit`) and xmake plugin (`plugins/format/xmake.lua`)
- Config path: `D:/clangformat/.clang-format` (machine-local, not in repo)

**llvm-cov / llvm-profdata — Code coverage (dev tool only):**
- Invocation: Via xmake plugin (`plugins/coverge/xmake.lua`)
- Data: `.profraw` files generated in `build/coverage/`, merged to `.profdata`

## Data Storage

**Databases:**
- No database servers (no SQL, no NoSQL, no embedded database)
- All data is file-based on local filesystem

**File Storage:**
- Local filesystem only
- Input: User-specified directories or files on disk
- Output: Encoded videos placed alongside inputs (or in user-specified output directory)
- State persistence: JSON state files (`.encro_state.json` by default) for resumable encoding/packing

**Caching:**
- In-memory immutable cache: `immer::map<fs::path, json::value>` in `appctx::RuntimeContext::VideoInfoCacheStore` for FFprobe results
- Thread-safe via `immer::atom<>` wrapper (`src/core/app_context.h:95-114`)
- No distributed cache or external caching system

**ZIP Packaging:**
- Library: `libzippp` (wraps `libzip`)
- Output: ZIP archives written to `<output>/packed/` directory
- Max archive size: ~490 MB (`kDefaultMaxArchiveGroupSize`)

## Authentication & Identity

**No authentication providers.** The tool operates entirely on local filesystem with no user authentication, accounts, or identity management.

## Monitoring & Observability

**Error Tracking:**
- No external error tracking service
- Built-in crash handler: SEH on Windows, signal handlers on Unix (`src/infra/crash_runtime.cpp`)
- Stacktrace capture: `std::stacktrace` (C++23) or fallback to `boost::stacktrace` (`src/infra/stacktrace.cpp`)
- All crashes written to stderr and `spdlog` logger

**Logs:**
- Framework: `spdlog` (with external `fmt`)
- Output: Console (via `terminal::` module) and spdlog sinks
- Configuration: Verbose mode (`--verbose`) enables spdlog debug output
- No external log aggregation service

**Progress:**
- Console: `indicators` library for progress bars, spinners
- Custom: `progress::ProgressContext` in `src/core/progress.h`
- Status text: `terminal::println()` with structured message kinds (Info, Warning, Error, Success, Hint, Heading, Prompt)

## CI/CD & Deployment

**Hosting:**
- No deployment platform detected
- Binary distribution: NSIS installer, source zip/tar archives via xpack (`xmake.lua:106-113`)

**CI Pipeline:**
- No CI/CD configuration files detected (no `.github/`, no `.gitlab-ci.yml`, no `Jenkinsfile`, no `azure-pipelines.yml`)

## Environment Configuration

**Required runtime dependencies (user-provided):**
- FFmpeg binary (for video encoding/transcoding; required unless `--pack-only`)
- FFprobe binary (for video metadata inspection)

**No env vars required.** All configuration is CLI-based.

**CLI flags (selected):**
| Flag | Purpose |
|---|---|
| `-i` / `--input` | Input file/directory path |
| `-I` / `--inputs` | Multiple input files (video only) |
| `-o` / `--output` | Output directory (supports aliases: `+`, `=`, `input://`, `common://`) |
| `--type` | Process type: `video` (default) or `picture` |
| `--output-format` | Output codec: `mp4` (HEVC NVENC) or `webp` |
| `--pack` | Enable ZIP packaging of encoded outputs |
| `--pack-only` | Skip encoding, only pack existing files |
| `--compress` | JPEG compress before packing (picture only) |
| `--image-quality` | FFmpeg image quality (2-31) |
| `--jobs` | Max parallel jobs |
| `--recursive` | Recurse into subdirectories |
| `--resume` / `--restart` | Job state resume/restart |
| `--state-file` | Explicit state file path |
| `--ffmpeg-path` | Explicit FFmpeg installation directory |
| `--yes` / `-y` | Skip confirmation prompts |
| `--flat` / `--keep` | Output directory layout |
| `--verbose` / `--verbose-echo` | Debug logging |
| `--full-progress` | Detailed progress bars (vs compact) |
| `--folder-summary` | Include one summary pic per subfolder |
| `--force-conflict-handling` | Enable collision-safe naming (`y`/`n`) |

## Webhooks & Callbacks

**Incoming:**
- None — no server component

**Outgoing:**
- None — no outbound HTTP or webhook calls

---

*Integration audit: 2026-05-07*
