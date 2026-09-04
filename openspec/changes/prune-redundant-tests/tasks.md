## 1. Batch 1 — Mechanical deletes (design.md I1 rows valid today)

- [x] 1.1 Delete `tests/packer_standalone_compile_tests.cpp` (no build-file edit needed — unit-target sources are globbed) and verify `xmake build tests` still succeeds
- [x] 1.2 Delete the can't-fail logging tests in I1: crash_integration `:25`/`:110`/`:151`, infra `:95`/`:37`, json `:335`/`:360` and the boost::json escaping quartet `:248`/`:271`/`:295`/`:316` (error_context `:205`/`:502` are NOT deleted — they are retained cases in the batch-3 merge); verify affected files compile
- [x] 1.3 Delete the can't-fail picture/pipeline/e2e/video/naming tests in I1 (`pipeline_picture_tests.cpp:264`, `encro_e2e_tests.cpp:1410`, `video_batch_execution_tests.cpp:83`, `video_progress_parser_tests.cpp:197`, `naming_strategy_tests.cpp:23`/`:39`, `picture_compress_tests.cpp:166`/`:243`); verify files compile (resolveColor pair, video_info negative-rate SECTION, and pipeline `:307` are kept — see design D1/I3)
- [x] 1.4 Delete the cmd tests in I1 (verbatim-default dups `cmd_cmd_tests.cpp:80`/`:274`, `:409`, `:665`, config_builder `:114`/`:784`/`:809`/`:834`, config `:137`) and the `[spike]` block `cmd_config_tests.cpp:415-544` including its `cliParse` helper (default-check folds and the preset-acceptance fold happen in batch 2); verify files compile
- [x] 1.5 Add the D2 replacement test in `cmd_config_tests.cpp`: store `image-quality` in a config file, run the real parse path without `-c/--compress`, assert the run succeeds with the stored value applied; verify new test passes via `xmake test-report --tag="[cmd]"`
- [x] 1.6 Run `xmake test-parallel` and confirm green; commit batch 1 as `test: delete meaningless tests that cannot fail or test only libraries`

## 2. Batch 2 — cmd cluster merges (design.md I3 cmd)

- [x] 2.1 Apply the cmd_cmd_tests.cpp merges from I3: fold the missing default assertions into `:58` then delete `:253`/`:332`/`:345`/`:372`; fold the `:829` preset-acceptance loop as a SECTION into `:821` then delete it; plus bare-invocation, image-quality long/short, app-level flags + preview, removed-flags pair, missing-values trio, COLUMNS cap pair, no-ANSI pair, auto-fit invariant per D3, usage-ordering collapse, commands-block shrink per D3 (descriptions + full-tier presence retained), color-injection folds, min-vmaf default half; verify `xmake test-report --tag="[cmd]"`
- [x] 2.2 Apply the cmd_config_tests.cpp / cmd_config_builder_tests.cpp / completion merges from I3 (preview twin display fold **carrying both sections**, negation row shrink, multi-input rejection pairs, single-positional family, single-field pass-through folds, completion_command merges); verify `xmake test-report --tag="[cmd]"` and `--tag="[completion]"`
- [x] 2.3 Run `xmake test-parallel`, confirm green; commit batch 2 as `test: merge duplicate cmd test assertion homes`

## 3. Batch 3 — logging + infra cluster (design.md I3 logging)

- [x] 3.1 Apply logging merges from I3: fold `:390`'s trailing-newline check into JsonFormatter Test 1 then delete it; fold `:407`'s elapsed segment into `:483`'s message then delete it; KEEP `:521` (separate timestamp implementation from crash_runtime); trim level-string test to 2 levels; error_context mechanics merge **retaining** `:205` (truncation-marker rendering) and `:502` (attribute FIFO eviction) as behavior-level cases; scoped_timer move sections, file_mgmt merges, snapshot state-table, crash_integration json on/off sections + timestamp trim; verify `xmake test-report --tag="[logging]"`
- [x] 3.2 Apply infra merges from I3 (terminal MessageKind table preserving every distinct assertion incl. Never-mode, toolchain resolve case unchanged + findFFmpeg relocation per I2, progress smokes + fitPostfixWithEta sections + EtaEstimator scaffolding share, run_id merge) and the D6 logger-inventory spot checks; verify `xmake test-parallel` green
- [x] 3.3 Commit batch 3 as `test: consolidate logging and infra test duplicates`

## 4. Batch 4 — video cluster + fake-tool move (design.md I2 + I3 video)

- [x] 4.1 Create `tests/fake_tool_tests.cpp`, move the 10 `[fake-tool]` cases and `runFakeTool`/`encodeArg` helpers from `encode_probe_tests.cpp:1012-1199` verbatim (no build edit — glob); verify `[fake-tool]` tag passes
- [x] 4.2 Apply video merges/deletes from I3 (skip-encode mid-layer delete with survivor `video_batch_execution_tests.cpp:565`; KEEP `:804` batch-level attentionWarnings; decideCq sections, barDone smoke, audio-once fold, banner fold + NVENC delta-only pins, output-planning trio, parser guard sections, KEEP `:107` and fold `:122` as a section, progressPercent table, `:145` rename, quality tables + parser trio, probe-cache key table, video-info boundary pair, previewHint fold); verify `xmake test-parallel` green
- [ ] 4.3 Run full `xmake coverage` (per-file report, not `--summary`) and confirm every subprocess-orchestration source stays double-digit; commit batch 4 as `test: move fake-tool contract tests to their own file, merge video duplicates`

## 5. Batch 5 — picture / pipeline cluster (design.md I3 picture)

- [ ] 5.1 Move the two `pack::execute` cases from `picture_process_tests.cpp:19`/`:53` into `pack_execute_tests.cpp`; verify they pass in their new home
- [ ] 5.2 Apply the pipeline-vs-component merges from I3: delete the weak picture copies, merge mid-batch cancel pair keeping the stronger assertions; replace `:91`/`:378`'s name-blind assertions with real property checks (per-dir grouping / keep-layout structure) in one retained pipeline case, then delete the two copies and the vacuous `:307`; verify `xmake test-parallel` green
- [ ] 5.3 Apply picture_compress merges from I3 (buildCMD 4→1-2, success/rename merge, single/multi merge); run full `xmake coverage` per-file check; commit batch 5 as `test: single assertion home for picture workflow behaviors`

## 6. Batch 6 — pack / preview / naming cluster (design.md I3 pack)

- [ ] 6.1 Apply pack merges from I3 (packer Directory dup delete, pack_service pairs, pack_execute summary/default-strategy/cancel-tightening, forced-disambiguation fold into naming_strategy_tests); fold `naming_strategy_tests.cpp:33`'s Flat-default assertion into `:52` then delete it; verify `xmake test-report --tag="[pack-service]"`, `--tag="[packer]"`, and `--tag="[pack-execute]"`
- [ ] 6.2 Apply preview trims from I3 (filtergraph megatest trim, preview_process `:195` delete and `:256` fold; `:97` gains StdoutCapture + printed-once assertions before the e2e `:1866` fold lands in batch 7); verify `xmake test-parallel` green
- [ ] 6.3 Commit batch 6 as `test: merge pack and preview duplicate test cases`

## 7. Batch 7 — e2e cluster (design.md I3 e2e)

- [ ] 7.1 Apply e2e merges from I3 (log-hint fold, `:277`+`:303` merge — this becomes the native-error exit-code survivor for the batch-1 deletions — spaced-path section, failure-state sections, positional sections, summary-status folds, cache fold, preview-once fold into the strengthened `preview_process_tests.cpp:97`, real-ffmpeg single-input fold, config-standalone trim with unit survivor `cmd_config_tests.cpp:291-346`); verify `xmake build e2e_tests && xmake run e2e_tests` green
- [ ] 7.2 Run the opt-in completion suite once (`ENCRO_TEST_COMPLETION=1 xmake test-report --tag="[completion]"`) to confirm gates intact; commit batch 7 as `test: drop e2e cases duplicating unit coverage, merge split scenarios`

## 8. Final verification

- [ ] 8.1 Full `xmake test-parallel` run green; record measured case count before (~766) vs after and true up the proposal's estimate
- [ ] 8.2 Full `xmake coverage` per-file check: no subprocess-orchestration source below double-digit line coverage; `xmake tidy` shows no new test-code warnings
