## 1. CLI surface and configuration

- [x] 1.1 Add the `--video-webp` option to `registerProcessingFlags` in `src/cmd/cmd.cpp` (next to `-c/--compress`), bound to a new `CmdParseResult::videoWebp`, visible in the default help tier (absent from `kAdvancedLongNames`), and add it to the picture usage line. Verify with a help-output test asserting the option renders in `-h` under the processing group, still renders in `-hh`, and that the picture usage line advertises it.
- [x] 1.2 Carry the flag into `appctx::AppConfig` (`videoWebp`) from `src/cmd/config_builder.cpp`, and reject it outside picture runs next to the existing `--compress` rejection in `applyMediaOptionValidations`. Verify with config-builder tests asserting `-t picture --video-webp` sets the field and `--video-webp` without `-t picture` fails with an error naming the flag and the required process type.

## 2. Video scan for picture runs

- [x] 2.1 Expose the video extension list and a probe-free, size-gated scan for conversion from `src/video/video_info.{h,cpp}` (move the extension list out of the anonymous namespace so it stays single-sourced; apply the 32MB WebP input gate here). Verify with a unit test over a temp directory asserting the scan returns the recognized video extensions, honors recursion, skips a clip above the size gate with a warning, and does **not** skip an already-HEVC clip.

## 3. Shared WebP encoder entry point

- [x] 3.1 Add the WebP entry point to `src/video/video_encode_runner.{h,cpp}` taking the output format explicitly, and route the existing `-f webp` branch of `encodeVideo` through it, so both callers share one adaptive-quality implementation and one recipe (`encode_config.h` recipe unchanged). Verify with a parity test that drives both callers against the fake ffmpeg — the conversion caller with `config.outputFormat == "mp4"`, the video caller with `"webp"` — and asserts the recorded command lines request the same encoder, filter chain, loop setting, starting quality and size target.
- [x] 3.2 Add the conversion cache directory accessor to `src/core/work_dirs.{h,cpp}` (`<work-root>\.encro\webp\`) with the same hidden-directory treatment the picture cache gets. Verify with a work-dirs unit test asserting the resolved path and that it does not depend on `-q/--image-quality`.

## 4. Job state, resume and cache lifecycle

- [x] 4.1 Record the flag in `jobstate::ConfigSnapshot` (write, read and `configMatches`) and include it in the picture branch of `shouldEnableJobState` in `src/app/pipeline.cpp`, so a conversion-only picture run gets state by default. Verify with job-state tests: a snapshot written without the field does not match a run with the flag, two runs with the flag match, and a picture run with only `--video-webp` creates state while one with neither flag does not.
- [x] 4.2 Register per-video conversion tasks in the picture flow (`makeEncodeTask` with the final cached output path), run only the tasks `needsExecution` reports, and mark them running/succeeded/failed as the conversions settle. Before reconciling with the store, discard the cached output of every clip whose saved record is not already a succeeded conversion of the current source (missing, reset-by-changed-source, interrupted or failed), plus any cached output older than its source, so a stale file can never be restored as a finished conversion. Verify with a test that seeds a state whose conversion task is Succeeded and asserts the clip is not converted again, a test that an Interrupted task is re-run, and a test that replaces a converted clip's source — including with an older timestamp — and asserts the stale output is discarded, the clip is converted again, and the archive is built from the fresh output.
- [x] 4.3 Wire the conversion cache lifecycle to the existing run outcomes: keep it when a run is canceled or interrupted after conversion started, remove it when the run completes successfully, and start from an empty cache on `--restart` or config mismatch. Verify with a test asserting the directory's presence or absence for the successful, canceled and restart paths.

## 5. Conversion phase in the picture workflow

- [x] 5.1 Plan video entries in the picture workflow: entry naming per layout (flat picture prefix plus the `.webp` extension; the clip's path relative to the input under the keep layout), the same conflict-handling rules the picture entries use, the clip's own directory as the entry's grouping key, and the cached WebP as the packed source instead of the clip. Verify with unit tests over a mixed picture/video set asserting the planned entry names, the grouping key, and that no source clip appears in the pack inputs.
- [x] 5.2 Implement per-clip conversion execution: encode to a temporary path carrying a WebP extension and rename to the final cached path only after the encoder reports success, collecting a failure reason per failed clip, with the conversion cancellation and stop paths leaving no final output. Verify with tests asserting the final path exists only after a successful encode, that an interrupted encode leaves only a temporary file, and that failures are collected with their reasons.
- [x] 5.3 Implement the phase itself: its own progress bar and one announcement line, the video scan's count line, its own concurrency cap (`min(-j/--jobs, 2)`), the all-failures abort before packing, and the no-videos case where no phase runs. Verify with tests covering the cap, the abort, and the no-videos case.
- [x] 5.4 Wire the phase into both picture workflows so it runs after picture compression and before packing: `executeCompressPackWorkflow` (with `-c`) and `executeDirectPackWorkflow` (without `-c`). Verify with a workflow test asserting phase order and that no archive is written before the conversions settle.

## 6. End-to-end coverage

- [x] 6.1 e2e case: a picture run with `--video-webp` over a directory holding pictures and a clip produces archives containing both the picture entries and the `1000__<stem>.webp` entry, verified by reading the zip with the existing e2e helpers.
- [x] 6.2 e2e case: cancel a run while a conversion is held at a fake-tool gate file, then re-run the same command and assert from the invocation log that the finished clip is not converted again and that the resumed run's archive contains the converted entry.
- [x] 6.3 e2e case: the same directory without the flag logs no conversion invocation, so the flag-off path costs nothing.

## 7. Real-tool verification

- [x] 7.1 Add a `[real-ffmpeg]` smoke case that converts a short generated clip through the picture workflow and checks the packed entry with `ffprobe` (stream is webp, more than one frame); it auto-skips when ffmpeg is absent.
