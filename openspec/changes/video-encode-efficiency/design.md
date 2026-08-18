## Context

Two execution-path overheads — see proposal.md - Why:

1. The encoding monitor polls every 20 ms and each poll re-reads the **entire** ffmpeg `-progress` file via `readLastNLines` (whole-file `getline` from byte 0). ffmpeg appends one ~20-line block per `stats_period` (0.5 s default), so ~96% of polls read an unchanged file, and the read cost grows linearly with encode duration (O(T²) total I/O). The same whole-file read serves `parseSegmentEndUs` at every segment boundary.
2. `buildCMD()` never sets `-threads`, so CPU codecs (`libx264`/`libx265`) use ffmpeg's auto thread count (≈ cores) per process; N parallel workers multiply that by N. The default codec path (`hevc_nvenc`) is GPU-bound and unaffected.

## Goals / Non-Goals

**Goals:**
- Bounded, size-independent progress-file reads; no read when the file is unchanged.
- Progress parsing decoupled from stop-signal latency.
- CPU-codec encode threads capped to the machine so workers don't oversubscribe.

**Non-Goals:**
- Changing the default `--jobs` value or worker-count resolution (a behavior change needing its own measurement — see Open Questions).
- Changing nvenc codec paths (threads stay auto there).
- Changing progress bar semantics, format, or cadence as seen by the user.
- Parallelizing segment encodes (out of scope; separate design).

## Decisions

### D1: Progress file reads become tail reads

Replace whole-file `readLastNLines` with a tail read: open, `seekg` to `max(0, size - 64 KiB)`, read to EOF, split lines, and drop the first (possibly partial) line. 64 KiB covers hundreds of `-progress` blocks — far beyond the existing `kProgressTailLines = 32` window, so the parser keeps its "last N lines" semantics unchanged (the 32-line cap still applies after the tail read; both parsers only need the last matching line). The same function serves `parseProgressFile` and `parseSegmentEndUs`.

### D2: Stat-skip when the file is unchanged

The monitor keeps the last-seen file size per active `EncodingState`; each poll stats the file first and skips parsing entirely when the size matches. A stat is ~free versus a multi-MB read; combined with D1, unchanged-file polls cost one syscall and changed-file polls cost ≤64 KiB. Because `state.progressFilePath` is swapped per segment (each segment writes its own progress file, starting at size 0), the tracked size must be keyed by path — reset the recorded size whenever the progress file path changes, so the first poll of a new segment file reads it instead of stat-skipping against the previous file's size. (The segment progress files are small by construction, so the size check mainly helps the continuous path; the tail read keeps them bounded too.)

### D3: Decoupled cadence — parse at ~250 ms, stop at 20 ms

The monitor loop keeps its existing 20 ms wakeup (stop detection and the forensic snapshot need it), but progress parsing is throttled to one pass per ~250 ms via a `lastParseAt` timestamp. Bars update at most 4×/s — visually indistinguishable from 50×/s, and it cuts parse + render work by ~20×. The parse skip in D2 already short-circuits the expensive case; D3 removes the redundant cheap-case work too.

### D4: CPU codecs get `-threads max(1, hw_threads / workers)`

`buildCMD()` sets `-threads` only for non-nvenc codecs (libx264/libx265): `max(1, hardware_concurrency / worker_count)`, so total encode threads ≈ the machine's thread count instead of workers × cores. The flag must reach the command actually executed: for MP4 that is `buildSegmentEncodeConfig` (used by `encodeOneSegment` for every segment AND by probe encodes), while `buildEncodeConfig` is only built for validation/display. The cleanest shape is a `threads` field on `EncodeConfig` that `buildCMD()` renders in the CPU-codec branch, with both construction sites passing the same value. The worker count comes from the existing `EncodingExecutionContext::counters().workers` in the batch path and is threaded through `encodeVideo` → `encodeOneSegment` (and `encodeWebpWithTargetSize` where the webp branch ignores it). The verbose/no-progress path (`runEncodingWithoutProgress`) is a serial loop with no `EncodingExecutionContext` — it resolves `workers = 1`, so a serial encode gets `hw/1` threads. nvenc branches keep auto threads — the GPU is the bottleneck there and CPU decode threads are already light.

## Risks / Trade-offs

- [x264/x265 output differs bit-wise per thread count] → Same CRF/settings otherwise; size and quality are equivalent. No test asserts bit-identical output (e2e uses the fake tool; `[real-ffmpeg]` tests assert outcomes, not bytes). Accepted.
- [`-threads` interacts with x265 frame threading (wpp)] → `-threads` caps frame threads; slices/wpp still scale within the cap. Conservative direction (fewer threads than auto) only risks slight under-utilization, never oversubscription.
- [Tail read misses a mid-write block] → The next parse (250 ms later, or the D2 size-change trigger) picks it up; progress is sampled, not exact. Same tolerance as today.
- [64 KiB tail constant goes stale if ffmpeg's block size grows] → Block size is a constant of `-progress` format (~20 lines); 64 KiB is 30× headroom. A unit test pins the "long file" case.

## Migration Plan

Pure internal change: no CLI, state-file, or output-format impact. Rollback = revert; nothing persisted.

## Open Questions

- Whether the default `--jobs` (10) oversubscribes single-GPU nvenc boxes is deliberately deferred: it needs a throughput measurement (same input at `--jobs 1/2/4/10`) before changing a user-visible default. If the measurement shows a flat curve, a follow-up change can lower the default.
