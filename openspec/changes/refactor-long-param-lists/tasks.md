## 1. Aggregates in encode_config.h (foundation)

- [ ] 1.1 Add `SegmentEncodeSpec` (inputPath, segmentIndex, startUs, durationUs, tempOutputPath) and `EncodeProfile` (outputFormat, videoCodec, crf, EncodeInputSettings, workerCount) as plain structs with designated-initializer construction, no factories/validate
- [ ] 1.2 Rework `buildSegmentEncodeConfig` to take `(toolchain, SegmentEncodeSpec, EncodeProfile, progressFilePath)`; keep it the single construction path for segmented encodes; update its call sites (encode_probe, video_encode_runner, tests) flat with designated initializers
- [ ] 1.3 Update test call sites of `buildSegmentEncodeConfig` (config parity/invariant tests) without changing assertions

## 2. video_quality: QualityRequest

- [ ] 2.1 Add `QualityRequest` (ffmpegPath, originalPath, encodedPath, startUs, durationUs, originalVideoInfo, encodedHasLocalPts) in video_quality.h; change `measureSegmentQuality` to take it
- [ ] 2.2 Change `runVmaf`/`runSsim` to take `QualityRequest const&` + log/stats-file path, updating internal calls
- [ ] 2.3 Update all `measureSegmentQuality` call sites (encode_probe, preview_process, tests) to construct `QualityRequest` flat

## 3. encode_probe: redundancy removal + ProbeProgress

- [ ] 3.1 Remove `ffmpeg`/`info` parameters where purely derivable from ctx (measurePoint, runProbeEncode chain); resolve in-function with the identical expression the call site used
- [ ] 3.2 Add file-local `ProbeProgress` (progressCtx, slotBars, slotProgress, completed, updateOverall); rework `buildProbeTasks`/`buildProbeTaskSpec` to take it, updating `runProbePhase` construction
- [ ] 3.3 Keep probe-side workerCount semantics unchanged (max(1, config.maxParallelJobs)); decide per function whether read from ctx.config or passed down, no behavior change
- [ ] 3.4 Rework `buildProbeSegmentConfig` as a thin adapter mapping its parameters onto `SegmentEncodeSpec`/`EncodeProfile` (D4: remains the probe-side construction path into `buildSegmentEncodeConfig`)

## 4. video_encode_runner + video_batch_execution

- [ ] 4.1 Rework `encodeOneSegment`/`assembleSegments` to use the new aggregates; keep `if (!encodeOneSegment(...))` and `if (!assembleSegments(...))` call sites at the same nesting level
- [ ] 4.2 Rework `prepareEncodingExecution`/`runEncodingTasks` (7/6 params) with regrouping only where cohesive; update video_process.cpp call sites

## 5. preview_process

- [ ] 5.1 Add preview-local `WindowBatchSpec` aggregate (original, windows, plan, settings, probeRoot) and `BarSlot` aggregate (progressCtx, bar, windowBase); rework `encodeAndScoreAllWindows` to take `(ctx, WindowBatchSpec, BarSlot, windowEncodeFailed)`
- [ ] 5.2 Rework `encodeAndScoreWindow` (remove ffmpeg/info params, resolve from ctx; keep workers param as-is per D1) and `renderAndReportSingleInput`/`probeSingleInputPlan`/`runSingleInput` signatures accordingly, updating call sites in run()
- [ ] 5.3 Verify the lambda call site of `encodeAndScoreWindow` (deepest call site, indent 9) does not gain nesting

## 6. packer

- [ ] 6.1 Regroup `buildDirectoryPackPlan`/`packAllFilesInDirectory` (recursion + naming strategy + concurrency into one aggregate where cohesive); update pack_service.cpp/pack.cpp call sites
- [ ] 6.2 Rework `packSourceEntryChunks` to take a small aggregate of its group-limit parameters (maxGroupSize, maxFilesPerGroup) plus the kept entries/output args; keep grouping thresholds and behavior identical

## 7. Tests, format, and verification

- [ ] 7.1 Update remaining test call sites (unit tests touching refactored signatures: encode config parity, probe, quality, packer) without changing assertions
- [ ] 7.2 `xmake fmt -k` passes on all touched files
- [ ] 7.3 `xmake test-report` passes; `xmake test-parallel` passes (unit + e2e)
- [ ] 7.4 Manual diff review: no converted call site gained a control-flow level; max call-site indent in touched files unchanged or reduced