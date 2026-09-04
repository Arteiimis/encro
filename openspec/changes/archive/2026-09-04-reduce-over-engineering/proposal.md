## Why

A repo-wide ponytail over-engineering audit (read-only; every candidate verified with repo-wide `rg` against `src/` and `tests/` before reporting) found ~860 lines of dead weight and redundant machinery in the audit scope, ~730 of them actionable (the include_cleaner plugin, ~120 lines, is deferred by user decision): zero-caller functions and fields, duplicate helper implementations, test-only API surface on production headers, duplicated test scaffolding, and one archived decision (`ENCRO_FAKE_FFMPEG_PROGRESS_PAD` kept "for mechanism contract" in test-suite-cleanup) that no test ever exercised. None of it changes behavior — it is noise that makes every future change land next to a dead twin.

## What Changes

All changes are internal refactors: no spec-level behavior changes, no user-visible CLI/file-format changes. Verification is the existing unit + e2e suites as regression gate (deleted symbols are zero-caller, verified by `rg`; deleted test cases are strict duplicates of surviving ones).

### 1. Dead code deletion (`src/`, `tests/`)

- Delete zero-caller functions/fields/params (zero production callers — `Store::flush()` additionally has 24 test call sites in job_state_tests/pack_execute_tests/video_batch_execution_tests that must be dropped or migrated, and the deleted pack symbols below have test callers that move to surviving APIs): `jobstate::primaryTargetPath`, `Store::flush()`, `TaskContext::stopRequested()`, `job_state_detail.h` orphan declarations (`buildFallbackStateFilePath`, `commonParent`), `progress::makeBar`/`addBar`/`setCursorVisible` (header exports, only internal use), `resolveVideoOutputPath()`, `EncodeConfig.outputPath` chain (written but never read in production — every construction sets `outputFilePath` or `tempOutputPath`, so the `buildOutputPath()` fallback is unreachable outside tests; CLI `-o` and job-state `outputPath` are separate structs and survive), `ProgressData.status`, `videoquality::mean()`, `isVmafLogEmpty()` (covered by `scores.empty()`), `VideoProbe.audioCodec`, `WebpEncodeStep.pid`, `EncodedVideoPackFile.sourcePath`, `runScoringCommand`'s unused `ffmpegPath` param (single call site), `SummaryConfig::prefix`, `LOG_TRACE` macro (zero uses), 3-arg `exec2` overload with `mergeStdErr` (zero callers), `mapZipEntryCompression()` (zero callers), `readOutputLayout`/`readPictureFolderSummary` one-line wrappers, `printHelp` 3-line wrapper, `entryNameTransform` param (both sites pass identity), `metricText()` wrapper, `makeSlotLabel`/`getStateLabel` duplicate one-liners, `addCompressTask`'s unread `std::error_code&` out-param.
- **BREAKING (internal API only, zero production callers)**: delete test-only pack API — `Packer::groupPackFiles`/`groupPackFilesWithSubparts`, `PackGroupInput`, `PackGroupPartition`, `PackService::runPackPlan` (both branches byte-identical; `store == nullptr` check dead), `runDirectoryPackWorkflow`, `packAllFilesInDirectory`; delete never-set `NamingConfig::zipNameStrategy` field + its branch in `resolveZipNameStrategy`, and `PackPlan::progressLabelForIndex` + `resolveProgressLabelForIndex` (no producer assigns it).
- Delete `ENCRO_FAKE_FFMPEG_PROGRESS_PAD` knob in `fake_media_tool.cpp` (drop the pad-filler block; the tool always emits `frame=10` and the knob defaults to pad=0, so hardcoded output is byte-identical). **This reverses the test-suite-cleanup decision** (`openspec/changes/archive/2026-08-28-test-suite-cleanup/`) that kept it for "mechanism contract" — no test sets it, and the PROGRESS_FRAMES sibling was already removed on identical grounds. Progress emission stays env-configurable via the remaining `ENCRO_FAKE_*` knobs.

### 2. Stdlib / native replacements

- `readWindowsEnvPath` (logging/setup.cpp) → existing `processenv::readEnvVar` (`_dupenv_s`).
- `readHelpColumnsOverride` (cmd.cpp) → existing `consolewidth::parsePositiveColumnCount`.
- `seconds(micros)` duplicated 4×: three identical free functions (video_quality.cpp, preview_filtergraph.cpp, preview_process.cpp) plus a string-returning lambda in encode_config.h — one shared `microsToSeconds` (double-returning) replaces the free functions; the lambda's string form is kept or unified per its single use.
- `parseDouble`/`parseFraction` duplicated verbatim (video_info.cpp, preview_process.cpp) → hoist to `video_workflow_utils.h`.

### 3. Equivalent restructuring

- Fold `parallel` module (parallel.h/cpp, `runIndexedTasks` — single caller, re-clamps what the caller already computed) into `task_executor`; delete the module.
- Merge `ProbeRootGuard`/`createProbeRoot` (encode_probe.cpp) with `PreviewProbeRootGuard`/`createPreviewProbeRoot` (preview_process.cpp) **preserving each site's retry constants** (6×/500ms+LOG_WARN vs 3×/200ms silent); merge `probeLowSide`/`probeHighSide` into one parametrized `probeSide`.
- `buildGroupOrdinalRanges` (4 overloads: two byte-identical `Impl`s + two delegating wrappers) → one template over `vector<vector<T>>`.
- `makeSubcommandHelpFormatter` → parameterize `makeHelpFormatter` over the option source.
- Dedup: `noteStopRequest` (2 TUs), score-text formatting (`formatScore` for the inline block in preview_process.cpp:243-254), `runEncodingWithoutProgress` vs `createEncodingState` field fill, `executeDirectPackWorkflow` vs `scanPictures` block, `readAllVids`/`readAllVidsFromFiles` common tail, `resolveWorkRoot` webp/generic branches, duplicate constants (3× `10'000'000` µs, 2× 20 MB, plus `packer.h`'s three identical `490*1024*1024` default args vs the divergent 500 MB twin in `pack_types.h`), `formatDefaultStr` vs inline `(=...)`.
- Inline one-line wrappers: `readOutputLayout`, `readPictureFolderSummary`, `printHelp`.

### 4. Test infrastructure cleanup

- Merge `pack_service_mock_tests.cpp` into `pack_service_tests.cpp`: the "mock" file tests the real `PackService` with the same scaffolding; cases 6–9 re-verify zip creation already covered by "packGroups packs grouped files". Keep the failure-path cases (2, 4, 5), port case 1 (happy path of a deleted API) onto its surviving replacement, delete the rest.
- Merge `StdoutCapture`/`StderrCapture` (byte-identical ~90-line mirror pair in test_utils.h) into one helper parameterized on the fd / underlying stream.
- Delete `writeFile`/`touchFile` 1-line aliases; migrate ~150 call sites to `writeTextFile` (with its default-content argument).

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None — this is a pure refactor: no spec-level behavior changes (the only observable surfaces — CLI, file formats, job-state, progress, encoding/pack semantics — are untouched). `skip_specs: true` is set in `.openspec.yaml` per the remove-immer-simplify-locks precedent for internal refactors.

## Impact

- `src/core/` (parallel.* deleted, job_state.*, job_state_store.cpp, task_executor.*, work_dirs.cpp, progress.h, job_state_detail.h)
- `src/cmd/` (cmd.cpp, option_specs.h, config_builder.cpp, config_store.h), `src/logging/` (logging.h, setup.cpp), `src/utils/utils.{h,cpp}`
- `src/video/` (video_output_planning, encode_config, video_encode_runner, video_batch_execution, encode_probe, video_quality, video_info, video_progress_parser, video_encoding_state, video_process, video_workflow_utils.h)
- `src/picture/picture_process.cpp`, `src/preview/preview_process.{h,cpp}`, `src/preview/preview_filtergraph.cpp`
- `src/pack/` (pack.{h,cpp}, pack_service.{h,cpp}, packer.{h,cpp}, packer_types.h, pack_plan_internal.h, pack_types.h)
- `src/infra/crash_runtime.{h,cpp}` (unchanged — `reportCaughtException`/`reportUnknownException` are production-called from main.cpp and stay)
- `tests/` (test_utils.h, pack_service_mock_tests.cpp, pack_service_tests.cpp, packer_tests.cpp, video_output_planning_tests.cpp, encode_probe_tests.cpp, job_state_tests.cpp, pack_execute_tests.cpp, video_batch_execution_tests.cpp, naming_strategy_tests.cpp, ~164 `writeFile`/`touchFile` call sites), `tests/e2e/` (e2e_test_utils.{h,cpp}, fake_media_tool.cpp, encro_e2e_tests.cpp)
- No new dependencies; no dependency removals. Style preserved (East const, trailing returns, C++26, clang-format).