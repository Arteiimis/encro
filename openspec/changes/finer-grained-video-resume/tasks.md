## 1. Job State Schema

- [x] 1.1 Write failing tests for new optional `TaskRecord` fields `segmentIndex` / `resumeTimeUs` round-tripping through JSON (write + reload preserves values, absent fields stay `nullopt`)
- [x] 1.2 Add `segmentIndex` / `resumeTimeUs` optional fields to `TaskRecord` in `src/core/job_state.h` and their serialization in `src/core/job_state.cpp` (backward-compatible read)
- [x] 1.3 Write failing tests for segment-aware restore in `normalizeExistingTask`: segmented task with partial output file is NOT restored to Succeeded; segmented task with all segments + missing final output → concat-only rerun state; task without segment records keeps file-level behavior
- [x] 1.4 Implement segment-aware restore logic in `normalizeExistingTask` (`src/core/job_state.cpp:424-452`): key restore off segment records for segmented tasks, keep `actionTargetExists` path for non-segmented tasks
- [x] 1.5 Write failing test: `clearExecutionState` (used by `--restart`) resets segment fields
- [x] 1.6 Update `clearExecutionState` to reset segment fields

## 2. Segment Temp Directory

- [x] 1.1 Write failing tests for the segment-dir helper: deterministic per-task path, created on demand, removed recursively
- [x] 1.2 Implement segment-dir helpers (create/list/remove) in `src/video/` (path from task id hash under `%TEMP%/encro/segments_<hash>/`; holds segments, list file, and extracted audio)
- [x] 1.3 Write failing test: on `--restart`, stale segment files are removed
- [x] 1.4 Hook stale-segment cleanup into the `--restart` / `clearExecutionState` path

## 3. Segmented Encode Pipeline

- [x] 1.1 Write failing tests for per-segment ffmpeg command construction in `EncodeConfig`: input seek `-ss <t>`, `-t <dur>`, `-force_key_frames pts:<t>`, `-an`, `-f mpegts` output to `seg_<k>.ts`
- [x] 1.2 Extend `EncodeConfig` / `buildCMD()` in `src/video/encode_config.h` with segment parameters (start time, duration, segment index, temp output path, per-segment progress file)
- [x] 1.3 Write failing tests for audio extraction: `-vn -c:a copy` to the temp audio file; fallback to single `-c:a aac` encode when copy fails; skipped for inputs without an audio stream
- [x] 1.4 Implement the audio extraction step (exec2 invocation, copy-with-fallback, no-audio detection)
- [x] 1.5 Write failing tests for the segment loop: N segments invoked in order for an MP4 video task, each via `exec2`, stop-signal checked per segment
- [x] 1.6 Implement the segment loop in the encode runner (`src/video/video_encode_runner.cpp` / `video_batch_execution.cpp`): encode segments 0..M-1, seed from `resumeTimeUs` on resume
- [x] 1.7 Write failing tests for per-segment completion handling: read final `out_time_us`/`frame=` from the segment's progress file, update `segmentIndex` / `resumeTimeUs`, forced state flush at segment boundaries
- [x] 1.8 Implement segment-completion bookkeeping + forced flush (markProgress with forced flag)
- [x] 1.9 Write failing tests for the assembly step: `-f concat -safe 0 -i list.txt -i <audio> -map 0:v -map 1:a -c copy -y <output>`; no-audio variant omits audio input/map; rerun when segments complete but output missing; missing audio temp → re-extract; task marked Succeeded only after assembly succeeds
- [x] 1.10 Implement the assembly step + segment-dir cleanup on success
- [x] 1.11 Write failing tests for segment-aware progress: `EncodingState` base-frame offset from `resumeTimeUs` × fps, monitor reports offset + current-segment frames
- [x] 1.12 Implement progress offset in `src/video/video_encoding_state.cpp` (base offset seeded per segment, total-frames probe unchanged)

## 4. E2E Tests

- [x] 1.1 Add e2e scenario: interrupted MP4/HEVC encode resumes at the first uncompleted segment (fake ffmpeg toolchain), completed segments not re-encoded
- [x] 1.2 Add e2e scenario: concat-only resume when all segments exist but final output missing
- [x] 1.3 Add e2e scenario: missing temp segment file on resume → re-encode from the first missing segment
- [x] 1.4 Add e2e scenario: `--restart` cleans stale segments and starts fresh
- [x] 1.5 Add e2e scenario: audio extracted exactly once (fake toolchain asserts a single audio invocation, segments carry `-an`) and muxed back at assembly

## 5. Verification

- [x] 1.1 Run `xmake format -k check` and fix any violations
- [x] 1.2 Run `xmake build tests && xmake run tests` (all tags) and `xmake build e2e_tests && xmake run e2e_tests` until green
