## 1. Batch 1 — Delete meaningless tests (design.md I1)

- [ ] 1.1 Delete `tests/packer_standalone_compile_tests.cpp` and remove it from the unit target sources; verify `xmake build encro_tests` (unit target) still builds
- [ ] 1.2 Delete the can't-fail logging tests listed in I1 (crash_integration `:25`/`:151`, infra `:95`/`:37`, json `:390`/`:335`/`:360`/`:248`/`:271`/`:295`/`:316`, error_context `:205`/`:502`); verify affected files compile
- [ ] 1.3 Delete the can't-fail picture/pipeline/e2e/video/naming/progress tests in I1 (`pipeline_picture_tests.cpp:264`/`:307`, `encro_e2e_tests.cpp:1410`, `video_batch_execution_tests.cpp:83`, `video_progress_parser_tests.cpp:197`, `video_info_tests.cpp:275` negative-rate SECTION, `naming_strategy_tests.cpp:23`/`:33`/`:39`, `picture_compress_tests.cpp:166`/`:243`, `progress_tests.cpp:89` pair); verify files compile
- [ ] 1.4 Delete the can't-fail cmd tests in I1 (defaults dups, preset-values tautology, colored-columns subsumed, help-non-empty, image-quality min/max pass-throughs, negation-absent, config_builder `:114`/`:834`) and the `[spike]` block `cmd_config_tests.cpp:415-544` including its `cliParse` helper; verify files compile
- [ ] 1.5 Add the D2 replacement test in `cmd_config_tests.cpp`: store `image-quality` in a config file, run the real parse path without `-c/--compress`, assert the run succeeds with the stored value applied; verify new test passes via `xmake test-report --tag="[cmd]"`
- [ ] 1.6 Run `xmake test-parallel` and confirm green; commit batch 1 as `test: delete meaningless tests that cannot fail or test only libraries`

## 2. Batch 2 — cmd cluster merges (design.md I3 cmd)

- [ ] 2.1 Apply the cmd_cmd_tests.cpp merges from I3 (bare-invocation, image-quality long/short, app-level flags + preview, removed-flags pair, missing-values trio, COLUMNS cap pair, no-ANSI pair, auto-fit invariant per D3, usage-ordering collapse, commands-block shrink, color-injection folds, min-vmaf default half); verify `xmake test-report --tag="[cmd]"`
- [ ] 2.2 Apply the cmd_config_tests.cpp and cmd_config_builder_tests.cpp merges from I3 (preview twin display fold, negation row shrink, multi-input rejection pairs, single-positional family, single-field pass-through folds, completion_command merges); verify `xmake test-report --tag="[cmd]"`
- [ ] 2.3 Run `xmake test-parallel`, confirm green; commit batch 2 as `test: merge duplicate cmd test assertion homes`

## 3. Batch 3 — logging + infra cluster (design.md I3 logging)

- [ ] 3.1 Apply logging merges from I3 (json suffix extraction, level-string trim, timestamp schema home, error_context mechanics merge + attribute sections + traits test, scoped_timer move sections, file_mgmt merges, snapshot state-table, crash_integration json on/off sections + timestamp trim); verify `xmake test-report --tag="[job-state]"` or per-file runs
- [ ] 3.2 Apply infra merges from I3 (terminal MessageKind table, toolchain fold, progress smokes + fitPostfixWithEta sections + EtaEstimator scaffolding share, run_id merge) and the D6 logger-inventory spot checks; verify `xmake test-parallel` green
- [ ] 3.3 Commit batch 3 as `test: consolidate logging and infra test duplicates`

## 4. Batch 4 — video cluster + fake-tool move (design.md I2 + I3 video)

- [ ] 4.1 Create `tests/fake_tool_tests.cpp`, move the 10 `[fake-tool]` cases and `runFakeTool`/`encodeArg` helpers from `encode_probe_tests.cpp:1012-1199` verbatim, add the file to unit target sources; verify `[fake-tool]` tag passes
- [ ] 4.2 Apply video merges/deletes from I3 (skip-encode and unreachable-floor mid-layer deletes, decideCq sections, barDone smoke, audio-once fold, banner fold + NVENC delta-only pins, output-planning trio, parser guard sections + large-file deletes + progressPercent table + `:145` rename, quality tables + parser trio, probe-cache key table, video-info boundary pair, previewHint fold); verify `xmake test-parallel` green
- [ ] 4.3 Spot-check `xmake coverage --summary` keeps every subprocess-orchestration source double-digit; commit batch 4 as `test: move fake-tool contract tests to their own file, merge video duplicates`

## 5. Batch 5 — picture / pipeline cluster (design.md I3 picture)

- [ ] 5.1 Move the two `pack::execute` cases from `picture_process_tests.cpp:19`/`:53` into `pack_execute_tests.cpp`; verify they pass in their new home
- [ ] 5.2 Apply the pipeline-vs-component merges from I3 (delete weak picture copies, merge mid-batch cancel pair keeping the stronger assertions, fold `:91`/`:378` or fix their assertions per the naming-strategy survivor); verify `xmake test-parallel` green
- [ ] 5.3 Apply picture_compress merges from I3 (buildCMD 4→1-2, success/rename merge, single/multi merge); spot-check `xmake coverage --summary`; commit batch 5 as `test: single assertion home for picture workflow behaviors`

## 6. Batch 6 — pack / preview / naming cluster (design.md I3 pack)

- [ ] 6.1 Apply pack merges from I3 (packer Directory dup delete, pack_service pairs, pack_execute summary/default-strategy/cancel-tightening, forced-disambiguation fold into naming_strategy_tests); verify `xmake test-report --tag="[pack-service]"` and `[packer]`
- [ ] 6.2 Apply preview trims from I3 (filtergraph megatest trim, preview_process `:195` delete and `:256` fold); verify `xmake test-parallel` green
- [ ] 6.3 Commit batch 6 as `test: merge pack and preview duplicate test cases`

## 7. Batch 7 — e2e cluster (design.md I3 e2e)

- [ ] 7.1 Apply e2e merges from I3 (log-hint fold, same-run merge, spaced-path section, failure-state sections, positional sections, summary-status folds, cache fold, preview-once fold, real-ffmpeg single-input fold, config-standalone trim); verify `xmake build e2e_tests && xmake run e2e_tests` green
- [ ] 7.2 Run the opt-in completion suite once (`ENCRO_TEST_COMPLETION=1 xmake test-report --tag="[completion]"`) to confirm gates intact; commit batch 7 as `test: drop e2e cases duplicating unit coverage, merge split scenarios`

## 8. Final verification

- [ ] 8.1 Full `xmake test-parallel` run green; record case count before (~766) vs after in the change notes
- [ ] 8.2 `xmake coverage --summary` spot-check: no subprocess-orchestration source below double-digit line coverage; `xmake tidy` shows no new test-code warnings
