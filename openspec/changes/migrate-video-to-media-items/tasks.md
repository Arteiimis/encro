## 1. `EncodingState` becomes the item (additive, no behaviour change)

- [ ] 1.1 Move `EncodingState`, `EncodingStatePtr` and `EncodingStateList` from `src/core/app_context.h:72-101` into `src/core/media_item.h`, leaving `app_context.h` with `AppConfig`, `ToolchainPaths` and `RuntimeContext`; verify `xmake build encro` succeeds and `rg -n "struct EncodingState" src` matches only the new header
- [ ] 1.2 Update the includes in `src/video/video_batch_execution.h`, `video_encode_runner.h`, `video_encoding_state.cpp` and `src/picture/picture_video_webp.cpp:124`; verify `xmake test-report --tag="[picture]"` and `--tag="[video-process]"` pass with no assertion edits
- [ ] 1.3 Add the `outcome` field and the `id()` / `label()` / `source()` / `target()` / `outcome()` accessors to `EncodingState` per design.md D1; verify a new case asserts a default-constructed `EncodingState` satisfies `mediaitem::Item` (a `static_assert` is enough) and the suite still passes
- [ ] 1.4 Add the pointer overload to `runStage` (design.md D2) with a direct case: a `std::vector<EncodingStatePtr>` runs, outcomes land on the pointees, and the counts match the value-overload case; verify `xmake test-report` passes

## 2. The maps die

- [ ] 2.1 Fold `plannedOutputFiles` into `item.target`: `video_process.cpp:313` keeps calling `planVideoOutputFiles` and writes the returned map onto the items; verify `tests/video/video_output_planning_tests.cpp` passes **unmodified** (`git diff --stat tests/video/video_output_planning_tests.cpp` is empty)
- [ ] 2.2 Fold `actionIds` into `item.id` at `prepareEncodeActions` (`video_process.cpp:96-136`) and delete the map; verify the job-state ids are byte-identical by running `xmake test-report --tag="[job-state]"` and the video resume cases
- [ ] 2.3 Fold `probeCqByInput` into `item.chosenCq` at `runProbeStage` (`video_batch_execution.cpp:385`); verify `xmake test-report --tag="[encode-probe]"` passes
- [ ] 2.4 Fold `results` into `item.outcome.state`, delete `collectEncodingResults` (`:440-455`), and re-point the summary (`video_process.cpp:519-598`) and `collectEncodedOutputFiles` (`:401-414`) at a **path-sorted view of the items** (design.md D7); verify the video summary cases pass with byte-identical text and that both consumers sort with `fs::path::operator<`, not `naming::stablePathString`
- [ ] 2.5 Add the order-pinning case design.md D7 requires: two inputs differing only in path case (e.g. `A.mp4` and `a.mp4`) produce a failure list and a pack file list in the same order as before the change; verify the case fails against a `stablePathString`-based sort and passes against `fs::path::operator<`
- [ ] 2.6 Fold `failureReasons` into `item.outcome.failureReason` at the three producing sites (`video_batch_execution.cpp:281,324,451`) and delete the print loop (`video_process.cpp:557-567`) in favour of the runner's shared one; verify the failure-line cases pass and `rg -n "failureReasons" src/video` returns nothing
- [ ] 2.7 Verify no path-keyed carrier is left in the flow **or its header**: `rg -n "path_map|std::map<fs::path" src/video/video_batch_execution.h src/video/video_batch_execution.cpp src/video/video_process.cpp` matches nothing outside the planner's return type and the pack request (the header is where `EncodingExecutionContext::plannedOutputFiles`, `actionIds` and `probeCqByInput` live at `:149-154`, so a grep that omits it passes with the carriers intact)

## 3. The encode phase moves onto the runner

- [ ] 3.1 Route the parallel encode phase (`video_batch_execution.cpp:567-674`) through `runStage` in the "caller draws its own bar" mode, keeping `EncodingProgressState` as the stage's own context; verify the `[video-process]` and `[encode-probe]` cases pass and the overall/slot bar text is byte-identical
- [ ] 3.2 Delete `EncodingBatchJob` and `EncodingBatchOutcome` (`video_batch_execution.h:28-50`), keeping `attentionWarnings`, `dryRun` and `encodeElapsed` in a small non-per-item summary struct; verify `rg -n "EncodingBatchJob|EncodingBatchOutcome" src tests` returns nothing — including `tests/video/encode_probe_tests.cpp`, whose scaffold builds both (`:445`, `:449-453`)
- [ ] 3.3 Update the construction sites in `tests/video/video_batch_execution_tests.cpp` and `tests/video/video_process_orchestration_tests.cpp`; verify those cases pass and their asserted narration text is unchanged (`git diff` shows no string literal edits in those files)
- [ ] 3.4 Verify job-state writes key off `item.id`: `rg -n "markRunning|markProgress|markSucceeded|markFailed|persistedElapsedMs" src/video` shows every call taking an `item`-derived id and no map lookup
- [ ] 3.5 Verify an in-flight state file still resumes: run the video resume e2e cases and `xmake test-report --tag="[job-state]"`; confirm `git diff --stat src/core/job_state*` is empty

## 4. The verbose-sequential path merges

- [ ] 4.1 Express `runEncodingWithoutProgress` (`video_batch_execution.cpp:277-340`) as the same `runStage` call with `maxConcurrency = 1` and `progress = nullptr`, keeping its `LOG_*` lines and the `"Echo enabled: progress bars disabled."` notice byte-identical; verify the verbose narration cases pass unmodified in text
- [ ] 4.2 Verify the per-file bookkeeping exists once: `rg -n "markRunning|markSucceeded|markFailed" src/video/video_batch_execution.cpp` lists one set of calls reachable from both modes
- [ ] 4.3 Verify no new `sleep_for` or fixed delay entered the tests: `xmake test-report --tag="[test-utils][meta]"` passes

## 5. Verification & commits

- [ ] 5.1 Run `xmake test-report` (full unit suite) and confirm zero failures
- [ ] 5.2 Run `xmake build e2e_tests && xmake run e2e_tests` and confirm the encode, stop/resume and pack end-to-end flows pass
- [ ] 5.3 Run `xmake test-parallel` and confirm every shard reports its assigned case count
- [ ] 5.4 Confirm the out-of-scope flows are untouched: `rg -n "runStage" src` matches no file under `src/pack/` or `src/preview/`, and `git diff --stat src/video/encode_probe.cpp src/video/video_info.cpp` is empty
- [ ] 5.5 Run `xmake fmt` before committing and confirm a second run produces no further changes; run `xmake tidy` and confirm no new diagnostics
- [ ] 5.6 Commit the planning artifacts first as their own `docs:` commit (proposal, design, tasks, `.openspec.yaml`), then implementation + tests + ticked `tasks.md` in one `refactor:` commit (English, conventional, subject < 72 chars, body wrapped at 80); verify with `git log --oneline -2` that the split is docs-then-refactor
