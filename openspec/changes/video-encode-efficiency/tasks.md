## 1. Tail-based progress file reads

- [x] 1.1 Write failing unit tests for a tail read replacing `readLastNLines`: file larger than 64 KiB returns only the last lines, partial first line at the seek boundary is dropped, small/empty files work, existing `kProgressTailLines` semantics preserved
- [x] 1.2 Implement the tail read in `video_progress_parser.cpp` (seek to `max(0, size - 64 KiB)`, read to EOF, drop the partial first line); both `parseProgressFile` and `parseSegmentEndUs` switch to it

## 2. Monitor stat-skip and decoupled cadence

- [x] 2.1 Add `lastProgressFileSize` tracking per active `EncodingState`; monitor stats the file first and skips parsing when the size is unchanged; test with a fake progress file that stops growing
- [x] 2.2 Throttle progress parsing to one pass per ~250 ms via a `lastParseAt` timestamp in the monitor loop, keeping the 20 ms wakeup for stop detection and forensic snapshots; test asserts parse count over a wall-clock interval
- [x] 2.3 Unit/integration tests for the segment progress path (`parseSegmentEndUs` on a growing file)

## 3. CPU codec thread cap

- [x] 3.1 Write failing tests for `buildCMD()` on a config with a `threads` field: non-nvenc codecs (libx264/libx265) include `-threads N`; nvenc codecs do not
- [x] 3.2 Add a `threads` field to `EncodeConfig` and render `-threads` in the CPU-codec branch of `buildCMD()`
- [x] 3.3 Thread the worker count through the real command path: `encodeVideo` → `encodeOneSegment` → `buildSegmentEncodeConfig` (the command actually executed for MP4), and into probe encodes via the same builder; batch path takes it from `EncodingExecutionContext::counters().workers`, the verbose/no-progress serial path resolves `workers = 1`
- [x] 3.4 Regression: existing command-string unit tests updated; fake-tool e2e suite passes unchanged

## 4. End-to-end verification

- [x] 4.1 E2E test: long encode with the fake tool (progress file grows past 64 KiB) completes with correct final progress
- [x] 4.2 Run full verification: build, unit + e2e suites, `xmake test-report`; `[real-ffmpeg]` smoke if ffmpeg is on PATH
