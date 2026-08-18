## Why

Two internal overheads in the video encode execution path are pure waste with no user-visible benefit:

1. The progress monitor polls every 20 ms and, for every active file, **re-reads the entire ffmpeg progress file from byte 0** (`readLastNLines`) — but ffmpeg only appends ~20 lines every 0.5 s (`stats_period`). For long videos the file grows to hundreds of thousands of lines and the re-read cost becomes O(T²) I/O that steals CPU from the encodes themselves.
2. When the user encodes with a CPU codec (`libx264`/`libx265`), every worker process uses ffmpeg's default `-threads auto` (≈ core count). N parallel workers × N threads per process oversubscribes the machine and can reduce throughput below fewer workers.

## What Changes

- Progress file polling becomes incremental: the monitor stats the file first and skips the read entirely when the size is unchanged, and reads only a bounded tail (last 64 KB) when the file did grow, so cost per poll stays constant regardless of file age. (~96% of the pre-throttle polls hit unchanged files; after the cadence change below, ~half of the remaining parse passes do.)
- Progress parsing cadence drops from 20 ms to ~250 ms; stop-signal detection keeps its existing 20 ms latency path (uncoupled from progress parsing).
- CPU codec encodes pass `-threads max(1, hardware_threads / worker_count)` to ffmpeg, so total encode threads fit the machine instead of multiplying.

## Capabilities

### New Capabilities

- none

### Modified Capabilities

- none — both changes are internal performance optimizations with no user-visible semantic change (same commands structure and output quality/size; `-threads` only caps the encoder's internal thread count, which may alter the x264/x265 bitstream bit-wise but not its quality or size), so no spec-level behavior changes.

## Impact

- `src/video/video_progress_parser.cpp`: tail-read implementation replacing whole-file `readLastNLines`.
- `src/video/video_encoding_state.cpp`: monitor loop stat-skip + decoupled cadence.
- `src/video/encode_config.h`: `buildCMD()` adds `-threads` for CPU codecs; needs the worker count plumbed into the encode runner.
- `src/video/video_encode_runner.cpp`: passes worker count down to config construction.
- No CLI, config, or state-file changes.
