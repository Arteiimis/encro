## Why

The video encode/probe/preview pipeline and the packer accumulate long parameter lists (7 to 12 parameters on `buildSegmentEncodeConfig`, `buildProbeTasks`, `encodeAndScoreAllWindows`, `measurePoint`, `encodeAndScoreWindow`, `buildDirectoryPackPlan`, and friends). Readability suffers twice: signatures are hard to scan, and multi-line call arguments add visual nesting at already-deep call sites (up to 4 control-flow levels). Many parameters are redundant — derived from `AppContext` (`ffmpeg`, `info`, `workerCount`) — and genuine ones fall into recurring clusters (segment identity, quality request, progress plumbing) that are regrouped manually at every call site.

## What Changes

- **Collapse redundant parameters**: parameters derivable from `AppContext` (`ffmpeg` path, `videoInfoCache` entry; probe-side worker count from `config.maxParallelJobs`) are resolved inside the function instead of being threaded through call sites. Preview-side worker count is a caller-derived `clamp` value and stays a parameter.
- **Introduce parameter objects (plain data aggregates)** for recurring clusters, constructed with designated initializers only — no factories, no `Result`-returning builders, no validation logic in the objects:
  - `SegmentSpec`-style aggregate for segmented-encode inputs shared by `buildSegmentEncodeConfig` / `buildProbeSegmentConfig` (input path, segment index, start/duration, temp output path).
  - `QualityRequest`-style aggregate for `measureSegmentQuality` / `runVmaf` / `runSsim` (paths, time range, video info, PTS convention).
  - `ProgressSlot`-style aggregate for the progress plumbing repeated in `encode_probe` and `preview_process` (context, bars, callbacks, counters).
- **No full object-orientation**: pipelines stay free functions; state remains in `AppContext`.
- **No nesting increase**: all new aggregates are constructed flat at call sites; no new `if`/error branches are introduced around the refactored calls.
- **Public API** (`encode_probe.h`, `video_quality.h`, `packer.h`, `video_batch_execution.h`, `preview_process.h`): signatures change; behavior is unchanged. No external CLI surface changes.

## Capabilities

This is a pure refactor: no externally observable behavior changes, no requirement changes. `skip_specs: true` is set in `.openspec.yaml`; no spec deltas are created.

## Impact

- `src/video/encode_config.h`, `src/video/encode_probe.{h,cpp}`, `src/video/video_quality.{h,cpp}`, `src/video/video_encode_runner.cpp`, `src/video/video_batch_execution.{h,cpp}`
- `src/preview/preview_process.{h,cpp}`
- `src/pack/packer.{h,cpp}`, `src/pack/pack_service.cpp`, `src/pack/pack.cpp`
- Tests: existing behavior tests (encode config parity invariant, probe scoring, pack grouping) must stay green; test-only helpers that call the changed functions are updated mechanically.
- No new dependencies; C++26 / clang-format style preserved (East const, trailing returns, designated initializers already in use).