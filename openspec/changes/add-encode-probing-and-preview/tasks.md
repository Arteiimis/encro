# Tasks: Encode Probing and Preview

## 1. Shared Quality Measurement

- [x] 1.1 Add `src/video/video_quality.{h,cpp}` with `measureSegmentQuality(ffmpeg, original, encoded, startUs, durationUs)` running libvmaf and returning per-frame scores (p5/mean aggregation helper); unit tests with recorded libvmaf JSON fixtures (`[video-quality]`)
- [x] 1.2 Add SSIM fallback (HDR/VMAF-unavailable detection, p5 threshold mapping 97→0.985 / 95→0.980 / 90→0.970) plus "scores unparsable" error path; unit tests (`[video-quality]`)

## 2. Encode Probing

- [x] 2.1 Add pure CQ-decision function (input: per-CQ p5 scores, floor → output: chosen CQ + warning) with step-4 grid semantics and interpolation; unit tests incl. unreachable-floor case (`[encode-probe]`)
- [x] 2.2 Add probe orchestration: pick 2 uniform 10s segments, run probe encodes at {24,28,32} with step-4 extension bounded [16,40] using the production `EncodeConfig` construction path, probe artifacts in a per-run temp dir (never `videoseg` dirs) cleaned up after use; unit tests for segment picking, temp-dir isolation, and the config-mirroring invariant (`[encode-probe]`)
- [x] 2.3 Add `--min-vmaf` (default 95) and `--dry-run` flags to the CLI registry; cmd tests (`[cmd]`)
- [x] 2.4 Wire the probe phase into `runEncodingTasks` before the `readUserIpt` gate (MP4 output only, WebP path unchanged): parallel execution, stop-signal checks between probe points, per-file decision map, plan printing (chosen CQ, p5, estimated size, batch totals), `--dry-run` exit path, `--crf` bypass, short-video skip; unit tests (`[encode-probe]`)
- [x] 2.5 Plumb per-file CQ decisions into a new `EncodingState` field (copied when execution contexts are created after the gate) so `buildEncodeConfig`/`encodeOneSegment` use the chosen CQ; unit tests (`[encode-probe]`)
- [x] 2.6 Collect unreachable-floor warnings into the final summary "needs attention" list; unit tests (`[encode-probe]`)
- [x] 2.7 Show progress bars during the probe phase in the same style as the encode bars (per worker slot + Overall, per sub-step updates, cursor hidden, nothing on non-TTY); unit tests (`[encode-probe]`)

## 3. Preview Subcommand

- [x] 3.1 Add CLI11 `preview` subcommand (`<original> <encoded>` positionals, `--output`, `--start`, `--duration`, `--no-open`), a `preview -h` formatter branch, and route `appentry::run` to `preview::run` before `buildAppConfig` (skipping job-state setup); cmd tests incl. subcommand-precedence and fallthrough scenarios (`[cmd]`, `[cli-positional]`)
- [x] 3.2 Add `src/preview/preview_filtergraph.{h,cpp}` pure builder (fps normalization, scale-to-min, ORIGINAL/ENCODED labels via arial, per-segment time+score labels, hstack+concat); unit tests asserting exact graph strings (`[preview]`)
- [x] 3.3 Add `src/preview/preview_process.{h,cpp}`: probe both inputs (duration/fps/dims), 5×10s uniform window selection with full-comparison fallback for short videos, per-window `measureSegmentQuality` scoring, worst-window marking; unit tests (`[preview]`)
- [x] 3.4 Add preview generation: single ffmpeg command from the filtergraph (x264 crf 14 veryfast, `-pix_fmt yuv420p`, per-window atrim/concat audio with AAC fallback, `-y`), default output `<original-dir>/<original-stem>.preview.mp4`, `--start/--duration` manual mode with clamp/error handling, non-video/webp rejection errors; unit tests (`[preview]`)
- [x] 3.5 Add `infra/open_file.{h,cpp}` `openWithDefaultApp()` (ShellExecuteW) and wire auto-open with `--no-open` suppression; unit test with no-open path (`[preview]`)

## 4. Encode Summary Hint

- [x] 4.1 Print the `encro preview <original> <encoded>` hint in the encode summary (suppressed for `--dry-run`); unit test (`[encode-probe]`)

## 5. E2E and Fake Tool

- [x] 5.1 Extend `fake_media_tool` to serve the ffprobe fields the new features need (width/height, frame rate, bitrate), to skip writing output files for `-f null -` scoring invocations, and to tolerate VMAF/SSIM calls; e2e tests for probe→plan→prompt flow with `--yes`, `--dry-run` (no output files, incl. no probe artifacts), `--crf` bypass, and preview generation + `--no-open` (`[e2e]`)
- [x] 5.2 Add `[real-ffmpeg]` smoke tests: probing a ≥40s real video picks a CQ and completes an encode (re-run asserts the same decision); preview generation produces a playable file with correct duration (`[smoke]`)

## 6. Docs and Verification

- [x] 6.1 Update README: `--min-vmaf`, `--dry-run`, probing behavior and plan output, `encro preview` usage
- [x] 6.2 Run `xmake format -k`, full unit + e2e suites, and the post-change code review
