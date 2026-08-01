## Why

Resume-from-interruption only works at file granularity today: a video task is either `Succeeded` (skipped on resume) or re-encoded from scratch. An MP4/HEVC encode interrupted at 95% restarts the whole file, wasting hours of work. `lastFrameCount` is already persisted but never consumed.

## What Changes

- Video encode tasks producing MP4 output are encoded in fixed-duration, keyframe-aligned segments instead of a single ffmpeg pass.
- Each completed segment is recorded in the job state (count + encoded duration), force-flushed at segment boundaries.
- On resume, encoding continues from the first uncompleted segment instead of restarting the file. `Succeeded`/`Interrupted` restore decisions for segmented tasks key off segment records, not raw output-file existence.
- The audio track is extracted once before segmenting (losslessly copied when compatible, otherwise encoded once) and muxed back at assembly; segments are video-only, so audio is never re-encoded per segment and stays in sync with the original timeline.
- A final lossless concat + remux pass (`-c copy`) assembles the finished MP4 from the video segments and the extracted audio; completed segments and the temp audio file are cleaned up afterward.
- Progress reporting accounts for completed segments (overall % reflects frames encoded across all segments, not just the current one).
- WebP/picture encoding and non-video tasks are unaffected.

## Capabilities

### New Capabilities
- `video-frame-resume`: fine-grained (segment-level) resume for MP4 video encode tasks — segment-based encoding, per-segment persistence, resume-from-segment, final concat, segment-aware progress.

### Modified Capabilities
<!-- No existing specs (openspec/specs/ is empty) - this is the first capability spec. -->

## Impact

- `src/video/video_encode_runner.cpp` — per-segment ffmpeg invocations (input seek, forced keyframe, fixed duration), concat pass.
- `src/video/encode_config.h` — segment-aware command construction.
- `src/core/job_state.h/.cpp`, `src/core/job_state_store.cpp` — new optional `TaskRecord` fields (`segmentIndex`, `resumeTimeUs`), segment-flush behavior, restore-logic changes in `normalizeExistingTask`.
- `src/video/video_encoding_state.cpp` — progress offset for completed segments; monitor flush at segment boundaries.
- `src/video/video_batch_execution.cpp` — segment loop orchestration, concat step, temp segment dir lifecycle.
- Temp segment files in a per-task directory (survive interruption; deleted on success / on `--restart`).
- Tests: job-state schema/restore, runner segment invocation, e2e resume-at-segment.
