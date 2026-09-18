## Why

The MP4 resume decision — where a resumed attempt starts, whether anything is left to encode, and how much of the timeline the progress bar must already count — is derived in several places inside `src/video/video_encode_runner.cpp` and read back in `src/video/video_encoding_state.cpp`; its two hardest rules exist only as comments (the recorded segment count, not the muxer's list, is the authority for what is reusable, and a list reaching the timeline end completes a task even when the encoder cut fewer segments than the duration implies); its `n x 10 s` segment-mark arithmetic is multiplied twice — the resume stride (`video_encode_runner.cpp:630`) and the persisted progress mark (`:386`) — and converted once into the monitor's frame offset (`:671`); and `getEncodingProgress` reads the `baseFrameOffset`/`totalFrames` pair unsynchronized (`video_encoding_state.cpp:113-117`, outside the `mtx` their writer holds at `video_encode_runner.cpp:669-671`).

Why now: the segmented-encode path was reworked twice (`finer-grained-video-resume`, `single-pass-segmented-encode`) and its rules have stopped moving, so the segment facts are stable enough to be an interface instead of a function's local variables.

## What Changes

- **New module** `src/video/segment_plan.{h,cpp}`, namespace `videoseg`, beside `segment_dir.h`:
  - `planSegments(totalDurationUs, storedSegments, segmentDir, totalFrames) -> SegmentPlan` — the entry direction: directory scan, muxer-list parse, reusable-prefix rule, "nothing left to encode" rule, resume stride, and the frame offset the bar counts from.
  - `closedSegments(listPath, startNumber)` — the exit direction: the same muxer list, read as "how many segments has the encoder closed".
  - The segment-mark arithmetic (`n x kSegmentDurationUs`) defined once and shared by both directions.
- `video_encode_runner.cpp` delegates: `reusableSegments` (`:398`), `encodeComplete` (`:414`), the inline stride (`:630`), the offset computation (`:671`) and the post-run list read for the assembly input (`:739`) move behind the module's interface. The runner keeps process spawning, audio extraction, assembly, the job-state read (`:605` — the only production consumer of the recorded segment count) and the job-state write.
- `SegmentListWatch`'s poll loop (`watchSegmentList`, `:426-432`) reports through `closedSegments` instead of parsing the list itself; `Store::markSegmentProgress` stays in the runner.
- **Fix the unsynchronized read**: `getEncodingProgress` snapshots `baseFrameOffset` and `totalFrames` under `EncodingState::mtx`, the way it already snapshots `progressFilePath` (`video_encoding_state.cpp:99-103`).
- **Tests**: new `tests/video/segment_plan_tests.cpp` covers the rules e2e cannot reach cheaply — the list reaching the timeline end while the encoder cut fewer segments than the duration implies (with its in-flight negative), a gap in the on-disk prefix, and the frame-offset conversion. Every existing e2e resume case stays; nothing is deleted.

No intended behavior change: CLI, job-state format, reported progress and the generated ffmpeg command lines are untouched; the one semantics this change fixes is the unsynchronized progress snapshot below.

## Capabilities

### New Capabilities

None — the change moves existing decisions behind an interface and fixes the unsynchronized progress-offset read; apart from that fix, no observable behavior changes.

### Modified Capabilities

None — `video-frame-resume` already specifies the behavior being relocated (resume stride, segment progress persistence, concat-only resume, restart-from-first-missing-segment); this change makes the implementation match that spec more directly rather than changing it. `skip_specs: true` is set in `.openspec.yaml` per the precedent of `refactor-long-param-lists` / `remove-immer-simplify-locks` / `reduce-over-engineering`.

## Impact

- `src/video/segment_plan.h`, `src/video/segment_plan.cpp` (new; picked up by `add_files("src/**.cpp")`)
- `src/video/video_encode_runner.cpp` (segment derivation, watcher parse, watcher store write stays)
- `src/video/video_encoding_state.cpp` (locked snapshot of the offset pair)
- Reused, not modified: `src/video/encode_config.h` (`SegmentSeries`, `kSegmentDurationUs`) and `src/video/video_progress_parser.h` (`segmentBaseFrameOffset`, called from the new module).
- `tests/video/segment_plan_tests.cpp` (new; picked up by `add_files("tests/video/*.cpp")`), with the existing video and e2e suites unchanged
- No new dependencies; no dependency removals; no user-facing surface touched.

**Explicitly out of scope:** merging the three `10'000'000` µs constants (`kSegmentDurationUs`, `kProbeWindowDurationUs`, `kWindowDurationUs`). `reduce-over-engineering` recorded the rejection — they are distinct domain constants that happen to be equal.
