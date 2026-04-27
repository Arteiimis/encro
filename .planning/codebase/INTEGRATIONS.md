# External Integrations

**Analysis Date:** 2026-04-28

## APIs & External Services

**External CLI Tools (subprocess invocation):**
- FFmpeg - Video encoding/transcoding engine
  - SDK/Client: None (invoked as subprocess via `exec2()` in `src/utils/utils.cpp`)
  - Command construction: `EncodeConfig::buildCMD()` in `src/video/encode_config.h:81-109`
  - Location discovery: `findFFmpeg()` in `src/utils/utils.cpp:337-356` (searches PATH and optional install dir)
  - Configuration: `--ffmpeg-install-dir` CLI option stored in `AppConfig::ffmpegInstallDir` (`src/core/app_context.h:60`)

- FFprobe - Video/audio metadata extraction (stream info, codec detection, frame counts)
  - SDK/Client: None (invoked as subprocess via `exec2()`)
  - JSON output parsed via `boost::json::parse()` in `src/video/video_info.cpp:287-319`
  - Location discovery: `findFFprobe()` in `src/utils/utils.cpp:316-335`
  - Consumed by: video scan pipeline (`readAllVids()`, `finalizeVideoList()`, `getVidTotalFrames()`)

**No External APIs:**
- This is a local desktop CLI tool — no HTTP/HTTPS client code, no REST/gRPC/GraphQL APIs, no cloud service SDKs
- No Stripe, Supabase, AWS, Azure, or any third-party service integrations detected

## Data Storage

**Databases:**
- None — no database client or ORM used

**File Storage:**
- Local filesystem only
- Input: user-specified file/directory paths (`.mp4`, `.mkv`, `.avi`, `.mov`, `.flv`, `.wmv` video files; image files for picture processing)
- Output: encoded video files (`.mp4`/`.webp`), compressed images, ZIP archives
- State persistence: JSON snapshot files written to disk via `jobstate::Store::flush()` in `src/core/job_state.h`
- Temporary files: progress files written to `fs::temp_directory_path()` during encoding (`src/video/video_encode_runner.cpp:65-66`)

**Caching:**
- In-memory video info cache via `immer::map` in `RuntimeContext::videoInfoCache` (`src/core/app_context.h:93-113`)
- No persistent cache / no Redis / no Memcached

## Authentication & Identity

**Auth Provider:**
- None — local CLI tool, no authentication required

## Monitoring & Observability

**Error Tracking:**
- Built-in crash handler (`src/infra/crash_runtime.cpp`) captures stacktraces on unhandled exceptions and fatal signals
- Stacktrace capture uses either `std::stacktrace` (C++23/26) or `boost::stacktrace` fallback (`src/infra/stacktrace.cpp`)
- Crash messages written to spdlog (stderr fallback) with exit code 1

**Logs:**
- spdlog with external fmt (`src/infra/crash_runtime.cpp` uses `spdlog::default_logger_raw()`)
- Log levels used: `info`, `warn`, `error`, `debug`, `critical`
- No log aggregation service, no external monitoring

## CI/CD & Deployment

**Hosting:**
- Local installation (distributed as NSIS installer, zip, tarxz via xpack in `xmake.lua:96-103`)
- No cloud deployment platform detected

**CI Pipeline:**
- None detected — no `.github/workflows/`, no Jenkinsfile, no GitLab CI config, no CircleCI config

## Environment Configuration

**Required env vars:**
- None required for runtime (FFmpeg/FFprobe discovered via PATH or `--ffmpeg-install-dir`)

**Secrets location:**
- Not applicable — no secrets needed (no API keys, no credentials)

## Webhooks & Callbacks

**Incoming:**
- None — not a server process

**Outgoing:**
- None — no outbound HTTP/webhook calls

---

*Integration audit: 2026-04-28*
