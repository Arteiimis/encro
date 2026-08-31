## 1. D1 — Dead code deletion (src)

- [ ] 1.1 core: delete `jobstate::primaryTargetPath`, `Store::flush()`, `TaskContext::stopRequested()`, `job_state_detail.h` orphan decls (`buildFallbackStateFilePath`, `commonParent`); remove `progress::makeBar`/`addBar`/`setCursorVisible` from the public header
- [ ] 1.2 core: collapse `resolveWorkRoot` webp/generic duplicate branches in `work_dirs.cpp` into one root computation + suffix
- [ ] 1.3 video: delete `resolveVideoOutputPath()` (fn + decl); delete its test case in `video_output_planning_tests.cpp`
- [ ] 1.4 video: delete `EncodeConfig.outputPath` chain (`state.outputPath` → `EncodeExecutionPlan.outputPath` → `EncodeConfig.outputPath` → `buildOutputPath()` fallback); delete the fallback test in `encode_config_tests.cpp:221-247`
- [ ] 1.5 video: delete `ProgressData.status` (+ its parser test assertions), `videoquality::mean()`, `isVmafLogEmpty()`, `VideoProbe.audioCodec`, `WebpEncodeStep.pid`, `EncodedVideoPackFile.sourcePath`, `runScoringCommand`'s unused `ffmpegPath` param (single call site at video_quality.cpp:211)
- [ ] 1.6 picture/preview: delete `entryNameTransform` param of `buildPackEntryInputs`, `metricText()` wrapper, `makeSlotLabel`/`getStateLabel` duplicate one-liners, `addCompressTask`'s unread `std::error_code&` out-param
- [ ] 1.7 cmd/logging/infra/utils: delete `LOG_TRACE` macro, 3-arg `exec2` overload (`mergeStdErr`), `SummaryConfig::prefix` + its test-only writer, `readOutputLayout`/`readPictureFolderSummary` wrappers, `printHelp` wrapper; drop `Store::flush()`'s 24 test call sites in job_state_tests/pack_execute_tests/video_batch_execution_tests (or migrate to `tasks()`)

## 2. D2 — Pack API shrink (public headers, zero production callers)

- [ ] 2.1 Delete `Packer::groupPackFiles`/`groupPackFilesWithSubparts` + `PackGroupInput`/`PackGroupPartition`; port `packer_tests.cpp` callers to `groupPackEntries*`/`buildDirectoryPackPlan`
- [ ] 2.2 Delete `PackService::runPackPlan` (dead `store == nullptr` branch), `runDirectoryPackWorkflow`, `packAllFilesInDirectory`; port `pack_service_tests.cpp` callers to `pack::execute(PackRequest)`
- [ ] 2.3 Delete `NamingConfig::zipNameStrategy` + its branch in `resolveZipNameStrategy`; delete the "stays unset" assertion in `naming_strategy_tests.cpp`
- [ ] 2.4 Delete `PackPlan::progressLabelForIndex` + `resolveProgressLabelForIndex`/`makeSubsetProgressLabelResolver` default branch

## 3. D3 — Stdlib replacements + equivalent restructuring

- [ ] 3.1 logging: replace `readWindowsEnvPath` with `processenv::readEnvVar`; cmd: replace `readHelpColumnsOverride` with `consolewidth::parsePositiveColumnCount`
- [ ] 3.2 Share one `microsToSeconds` for the 4 duplicate definitions (encode_config.h, video_quality.cpp, preview_filtergraph.cpp, preview_process.cpp)
- [ ] 3.3 Hoist `parseDouble`/`parseFraction` duplicates (video_info.cpp, preview_process.cpp) into `video_workflow_utils.h`
- [ ] 3.4 Fold `parallel` module (`runIndexedTasks`) into `task_executor.cpp` as an internal function; delete parallel.h/parallel.cpp
- [ ] 3.5 Merge `ProbeRootGuard`/`createProbeRoot` with `PreviewProbeRootGuard`/`createPreviewProbeRoot` (preserving each site's retry constants: 6×/500ms+LOG_WARN vs 3×/200ms silent); merge `probeLowSide`/`probeHighSide` into one parametrized `probeSide`
- [ ] 3.6 Replace the 4 `buildGroupOrdinalRanges` overloads with one template over `vector<vector<T>>`
- [ ] 3.7 Parameterize `makeHelpFormatter` over the option source; delete `makeSubcommandHelpFormatter`; share `formatDefaultStr`
- [ ] 3.8 Dedup small items: `noteStopRequest` (video_encoding_state.cpp/video_batch_execution.cpp), score-text formatting (inline block at preview_process.cpp:243-254), `runEncodingWithoutProgress` field fill vs `createEncodingState`, `executeDirectPackWorkflow` scan block vs `scanPictures`, `readAllVids`/`readAllVidsFromFiles` common tail, duplicate constants (3× `10'000'000` µs, 2× 20 MB, three identical `490*1024*1024` default args in packer.h vs the divergent 500 MB twin in pack_types.h)

## 4. D4 — Test infrastructure cleanup

- [ ] 4.1 Merge `pack_service_mock_tests.cpp` into `pack_service_tests.cpp`: keep the failure-path cases (2, 4, 5), port case 1 (happy path of a deleted API) onto its surviving replacement (`pack::execute`), delete the rest (6–9 are zip-creation duplicates)
- [ ] 4.2 Merge `StdoutCapture`/`StderrCapture` into one `FileCapture` parameterized on `FILE*`; keep the two names as thin aliases; fix `test_utils_tests.cpp` if needed
- [ ] 4.3 Delete `writeFile`/`touchFile` aliases; whitelist-check all ~164 call sites with `rg`, sed to `writeTextFile`, `xmake fmt` before committing (pre-commit clang-format hook), review the diff
- [ ] 4.4 Delete `mapZipEntryCompression()` (e2e_test_utils.{h,cpp}, zero callers) and the `ENCRO_FAKE_FFMPEG_PROGRESS_PAD` pad-filler block in fake_media_tool.cpp (drop the knob; hardcoded `frame=10` output is byte-identical)

## 5. Verification & commits

- [ ] 5.1 Run `xmake test-report` after each batch (D1–D4) before committing; nothing but the batch's own deletions may fail
- [ ] 5.2 Run `xmake test-parallel` after D2 and D4
- [ ] 5.3 Commit per batch (English, conventional: `refactor:`; `test:` where test files dominate), ticking the change's `tasks.md` checkboxes in the same commit (planning `docs:` commit already landed as aece451)