## Why

MP4 video tasks are encoded one ffmpeg process per 10-second segment, so every segment pays a fresh NVENC session setup (measured 0.30 s; a bare ffmpeg start is only 0.14 s) plus an accurate `-ss` seek that decodes and discards frames up to the cut point. Between segments the encode engine sits idle, so a one- or two-file run never saturates the GPU (measured engine-busy duty cycle of 74% on a 1080p24 source with a 10.4 s GOP, and lower on slower-decoding sources) even though a single NVENC session already saturates the engine: 1, 2, 4 and 8 concurrent sessions all measured ~155-158 fps of aggregate 1080p HEVC p6 throughput. More parallelism cannot fix this; removing the per-segment restart can.

## What Changes

- Encode a task's video in **one ffmpeg process per file** using ffmpeg's `segment` muxer instead of one process per segment: no repeated NVENC session setup and no repeated input seeks.
- Keep the existing segment boundaries and resume granularity: the cut cadence stays at fixed 10 s boundaries, enforced with `-forced-idr 1 -force_key_frames "expr:gte(t,n_forced*10)"` (without `-forced-idr`, `-force_key_frames` is silently ignored by `hevc_nvenc`).
- Keep the segment files and the lossless `-c copy` concat assembly exactly as today; segments remain video-only, independently decodable, and audio still resolves once outside the segment path.
- Detect completed segments during the run from the segment muxer's live CSV list (`-segment_list ... -segment_list_type csv -segment_list_flags +live`) instead of per-segment progress files, and persist the same per-segment resume records to job state.
- Resume still re-encodes only the in-flight tail: continue with `-ss <completed * 10 s>` and `-segment_start_number <completed>` (verified frame-exact at 24 fps and 23.976 fps: no lost or duplicated frames).
- Segment progress reporting switches to the single continuous `-progress` frame counter plus a resume offset.
- Not in scope: the encoder probing phase (`--min-vmaf`), whose CPU-side XPSNR scoring is a separate, independent wall-clock cost; per-file concurrency defaults; any change to the encoder preset or quality policy.

## Capabilities

### New Capabilities

- (none)

### Modified Capabilities

- `video-frame-resume`: the segmented encoding requirement changes from "one ffmpeg pass per segment" to "one ffmpeg pass per file that writes segments", the progress/state requirement changes from per-segment progress files to the segment muxer's live segment list, and the resume requirement is restated in terms of `-segment_start_number`.

## Impact

- Code: `src/video/video_encode_runner.cpp` (`runSegmentedEncoding`, `encodeOneSegment`, `ensureAudioFile`, `assembleSegments`, `kSegmentDurationUs`), `src/video/encode_config.h` (`buildSegmentEncodeConfig`, `buildCMD` segment/forced-keyframe flags, new segment-muxer command), `src/video/video_progress_parser.{h,cpp}` (segment list parsing), `src/core/job_state.{h,cpp}` (resume records unchanged in shape; segment identity is still the 10 s index).
- Behavior: identical output container, codec, quality settings, audio handling and resume granularity; the intermediate segment files now come from the muxer (numbered files plus a CSV) instead of app-named files, and the work-directory layout changes accordingly.
- Probe encodes (`src/video/encode_probe.cpp`) keep using single-window encodes and are unaffected.
- Tests: unit coverage for the segment-muxer command and segment-list parsing; e2e coverage on the fake-tool path (segment list emission, resume continuation, concat assembly) and the existing real-ffmpeg smoke test.
- Performance evidence (RTX 3070 Laptop, 1080p24 H.264, 10.4 s GOP, probe skipped): 24.7 s / 74% engine-busy before, 18.2 s / 94% after, against an 18.3 s uninterrupted single-pass floor.
