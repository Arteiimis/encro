## Why

Picture runs are usually image sets that contain a few very short clips. The picture scan matches image extensions only, so those clips are invisible to the run: they are neither converted nor packed, and the user only discovers the omission after opening the archive. Converting them separately with `-t video -f webp` produces the same content but a second archive with a different naming scheme, so the two halves never sit together in one browsable set.

## What Changes

- Add a `--video-webp` flag to picture runs: videos found under the input are converted to animated WebP and packed into the same archives as the pictures.
- Reuse the existing video-to-WebP recipe unchanged (the one behind `-t video -f webp`): animated WebP, height capped at 960, `-loop 0`, adaptive quality bisection targeting <20MB. One recipe, one place — the same clip produces the same bytes through either command.
- Video entries use the picture entry-naming scheme: flat `1000__<name>.webp` entries alongside `1000__<name>.jpg`, following the run's layout and the same conflict-handling rules the pictures already follow, and grouped with the pictures of the clip's own source directory.
- Run conversion as its own phase inside the picture workflow: after picture compression, before packing, with its own progress line and its own concurrency cap; packing still starts only after every conversion has settled.
- Cache conversions on disk and resume them per video: completed conversions are restored from the cache, an interrupted conversion re-runs, a changed source re-converts, `--restart` clears the cache, and cancellation keeps it.
- Create job state by default for a picture run that converts videos, `-c` or not, and include the flag in the job-state config snapshot so runs differing in it never resume each other.

Non-goals: still-frame (poster) WebP output; a WebP quality knob; converting in pack-only mode or in video runs; letting a picture run pack a clips-only directory (a picture run still requires at least one picture); archive entry ordering by name (existing scan order stands); persisting the flag as a user-config key.

## Capabilities

### New Capabilities

- `picture-video-webp`: video-to-WebP conversion inside the picture workflow — the flag surface, what is scanned, the recipe, entry naming, the conversion cache and its resume rules, failure handling, and progress.

### Modified Capabilities

- `job-state-resume-matching`: a picture run with video conversion enables job state by default, and the conversion flag participates in job-state config matching.

## Impact

- CLI surface: `src/cmd/cmd.h`, `src/cmd/cmd.cpp`, `src/cmd/config_builder.cpp`, `src/core/app_context.h`
- Picture workflow: `src/picture/picture_process.cpp`
- Video encoder seam: `src/video/video_info.{h,cpp}` (a video scan that reports clips to convert rather than clips to re-encode), `src/video/video_encode_runner.{h,cpp}` (a WebP entry point usable without `-f webp`); `src/video/encode_config.h` recipe reused as-is
- Job state: `src/core/job_state.cpp` (config snapshot field) and `src/app/pipeline.cpp` (when a picture run enables job state)
- Work directories: `src/core/work_dirs.{h,cpp}` (conversion cache directory)
- Tests: unit coverage for the flag, the scan, entry naming, and resume decisions; e2e coverage of archive contents with the fake ffmpeg
