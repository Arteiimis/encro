## 1. `EncodingState` becomes the item (additive, no behaviour change)

- [x] 1.1 Move `EncodingState`, `EncodingStatePtr` and `EncodingStateList` from `src/core/app_context.h:72-101` into `src/core/media_item.h`, leaving `app_context.h` with `AppConfig`, `ToolchainPaths` and `RuntimeContext`; verify `xmake build encro` succeeds and `rg -n "struct EncodingState" src` matches only the new header
- [x] 1.2 Update the includes in `src/video/video_batch_execution.h`, `video_encode_runner.h` and `video_encoding_state.cpp`; `src/picture/picture_video_webp.cpp` needed no edit — it already included the header and the type kept its `appctx` namespace, so its diff is empty (the post-review header split moved that include to `core/encoding_state.h`, section 6); verify `xmake test-report --tag="[picture]"` and `--tag="[video-process]"` pass with no assertion edits
- [x] 1.3 Add the `outcome` field and the `id()` / `label()` / `source()` / `outcome()` accessors to `EncodingState` per design.md D1 (no `target()`: the concept drops it, and `plannedOutputFile` stays a plain field); verify a new case asserts a default-constructed `EncodingState` satisfies `mediaitem::Item` (a `static_assert` is enough) and the suite still passes
- [x] 1.4 Add the pointer overload to `runStage` (design.md D2) with a direct case: a `std::vector<EncodingStatePtr>` runs, outcomes land on the pointees, and the counts match the value-overload case; verify `xmake test-report` passes

## 2. The maps die

- [x] 2.1 Fold `plannedOutputFiles` into `item.plannedOutputFile`: `video_process.cpp:313` keeps calling `planVideoOutputFiles` and writes the returned map onto the items; verify `tests/video/video_output_planning_tests.cpp` passes **unmodified** (`git diff --stat tests/video/video_output_planning_tests.cpp` is empty)
- [x] 2.2 Fold `actionIds` into `item.id` at `prepareEncodeActions` (`video_process.cpp:96-136`) and delete the map; verify the job-state ids are byte-identical by running `xmake test-report --tag="[job-state]"` and the video resume cases
- [x] 2.3 Fold `probeCqByInput` into `item.chosenCq` at `runProbeStage` (`video_batch_execution.cpp:385`); verify `xmake test-report --tag="[encode-probe]"` passes
- [x] 2.4 Fold `results` into `item.outcome.state`, delete `collectEncodingResults` (`:440-455`), and re-point the summary (`video_process.cpp:519-598`) and `collectEncodedOutputFiles` (`:401-414`) at a **path-sorted view of the items** (design.md D7); verify the video summary cases pass with byte-identical text and that both consumers sort with `fs::path::operator<`, not `naming::stablePathString`
- [x] 2.5 Add the order-pinning case design.md D7 requires: two inputs differing only in path case (e.g. `A.mp4` and `a.mp4`) produce a failure list and a pack file list in the same order as before the change; verify the case fails against a `stablePathString`-based sort and passes against `fs::path::operator<`
- [x] 2.6 Fold `failureReasons` into `item.outcome.failureReason` at the three producing sites (`video_batch_execution.cpp:281,324,451`) and delete the print loop (`video_process.cpp:557-567`) in favour of the runner's shared one; verify the failure-line cases pass and `rg -n "failureReasons" src/video` returns nothing
- [x] 2.7 Verify no path-keyed carrier is left in the flow **or its header**: `rg -n "path_map|std::map<fs::path" src/video/video_batch_execution.h src/video/video_batch_execution.cpp src/video/video_process.cpp` matches nothing outside the planner's return type and the pack request (the header is where `EncodingExecutionContext::plannedOutputFiles`, `actionIds` and `probeCqByInput` live at `:149-154`, so a grep that omits it passes with the carriers intact)

## 3. The encode phase moves onto the runner

- [x] 3.1 Route the parallel encode phase through `runStage` in the "caller draws its own bar" mode: the stage passes `EncodingExecutionContext`'s own `ProgressContext` and the Overall bar index (`video_batch_execution.cpp:569-573`) with `StageSpec::setBarProgress = false`, plus a `postfix` that drives the flow's own bar math (`markFinished`/`updateOverall`/`overallText`, `:575-580`), so the runner's count fraction never touches the bar (design.md D5); verify the `[video-process]` and `[encode-probe]` cases pass and the overall/slot bar text is byte-identical
- [x] 3.2 Delete `EncodingBatchJob` and `EncodingBatchOutcome` (`video_batch_execution.h:28-50`), keeping the non-per-item values (`attentionWarnings`, `dryRun`, `skippedCount`, `encodeElapsed` and the `canceled` flag that `results == nullopt` signalled) in a small summary struct; verify `rg -n "EncodingBatchJob|EncodingBatchOutcome" src tests` returns nothing — including `tests/video/encode_probe_tests.cpp`, whose scaffold builds both (`:445`, `:449-453`)
- [x] 3.3 Update the construction sites in `tests/video/video_batch_execution_tests.cpp`; `tests/video/video_process_orchestration_tests.cpp` never built the batch types, so it needed no construction-site change (it gained the path-order case in 2.5); verify those cases pass and their asserted narration text is unchanged — the one renamed case title (`returns nullopt results` → `reports a canceled batch`) names a return value the summary no longer has
- [x] 3.4 Verify job-state writes key off `item.id`: `rg -n "markRunning|markProgress|markSucceeded|markFailed|persistedElapsedMs" src/video` shows every call taking an `item`-derived id and no map lookup
- [x] 3.5 Verify an in-flight state file still resumes: run the video resume e2e cases and `xmake test-report --tag="[job-state]"`; confirm `git diff --stat src/core/job_state*` is empty

## 4. The verbose-sequential path merges

- [x] 4.1 Express `runEncodingWithoutProgress` (`video_batch_execution.cpp:277-305`) as the same `runStage` call with `maxConcurrency = 1` and `progress = nullptr`, keeping its `LOG_*` lines and the `"Echo enabled: progress bars disabled."` notice byte-identical (`:408`); two consequences came with the merge and are accepted in design.md's Risks: the cursor stays visible, because a stage with `progress = nullptr` no longer hides it (`media_item.h:155-157`), and an exception escaping the encoder becomes a per-item failure instead of propagating (`task_executor.cpp:52-62`); verify the verbose narration cases pass unmodified in text
- [x] 4.2 Verify the per-file bookkeeping exists once: `rg -n "markRunning|markSucceeded|markFailed" src/video/video_batch_execution.cpp` lists one set of calls reachable from both modes
- [x] 4.3 Verify no new `sleep_for` or fixed delay entered the tests: `xmake test-report --tag="[test-utils][meta]"` passes

## 5. Verification & commits

- [x] 5.1 Run `xmake test-report` (full unit suite) and confirm zero failures
- [x] 5.2 Run `xmake build e2e_tests && xmake run e2e_tests` and confirm the encode, stop/resume and pack end-to-end flows pass
- [x] 5.3 Run `xmake test-parallel` and confirm every shard reports its assigned case count
- [x] 5.4 Confirm the out-of-scope flows are untouched: `rg -n "runStage" src` matches no file under `src/pack/` or `src/preview/`, and `git diff --stat src/video/encode_probe.cpp src/video/video_info.cpp` is empty
- [x] 5.5 Run `xmake fmt` before committing and confirm a second run produces no further changes; run `xmake tidy` and confirm no new diagnostics
- [x] 5.6 Commit the planning artifacts first as their own `docs:` commit (proposal, design, tasks, `.openspec.yaml`), then implementation + tests + ticked `tasks.md` in one `refactor:` commit (English, conventional, subject < 72 chars, body wrapped at 80); verify with `git log --oneline -2` that the split is docs-then-refactor

## 6. Post-Change Review follow-up

- [x] Run the `code-review` skill over the change's commits on all three axes, with the change as the spec source, once this reconciliation lands.
- [x] Fix the material finding: the item list held one item per scanned path, so `encro video a.mp4 a.mp4` reported `Encoded 2/2 videos` and handed the same output to the packer twice (`Packing 2 encoded video(s)...`), while the deleted `EncodeResultsMap` — a `std::map<fs::path,bool>` — counted the repeat once. `runScannedEncodingWorkflow` now keeps one item per unique input path in input order (`video_process.cpp`), and a case pins the one-item count, the `1/1` summary and the single pack input; it failed first with those two doubled lines.
- [x] Apply the accepted structural finding: `appctx::EncodingState` / `EncodingStatePtr` / `EncodingStateList` move out of `src/core/media_item.h` into `src/core/encoding_state.h`, so the contract header carries no flow state — it does not include the new header, `app_context.h` stays free of the type, and the `appctx` namespace is unchanged.
- [x] Defended: `StageSpec::setBarProgress` — the alternative is the flow counting its own completions, which is the duplication this change exists to remove, and the default keeps picture's and organize's call sites byte-identical.
- [x] Defended: the named per-item verbose wrapper — it exists to keep three `bugprone-lambda-function-name` warnings from returning (its three `LOG_*` lines would otherwise sit in the stage's `runOne` lambda).
- [x] Defended: the pointer/value equivalence case — task 1.4 mandates it.
- [x] Deferred: `EncodingOutcome`/`collectOutcome` bundling two fields for one log line — replacing it would move a mutex-guarded read to the log site.
- [x] Deferred: `maybePackOutputs` re-checking a flag its only caller checked — pre-existing, not this change's.
- [x] Deferred: `updateOverall` and the hook writing the same bar text — the two writers serve the monitor's periodic refresh and the per-completion hook, and deduplicating them would make the flow split its own bar update in half.
- [x] Deferred: a log line printing one number twice — pre-existing.
