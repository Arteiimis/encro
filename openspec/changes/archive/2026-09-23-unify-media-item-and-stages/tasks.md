## 1. The completion hook (no caller migrated yet)

- [x] 1.1 Add the hook case first: with `maxConcurrency = 1` and a known task list, `onTaskFinished` is called exactly once per task that ran, with `done` reaching `total`, and is **not** called for a slot the stop signal skipped (`testutils::ScopedStopSignalReset`, stop requested from a task body); verify the case fails to compile (red first) and record the compile error in the commit body
- [x] 1.2 Add `std::function<void(std::size_t done, std::size_t total)> onTaskFinished` to `TaskPlan` (`src/core/task_executor.h:28-33`), invoke it from the worker loop after the outcome slot is written, passing the executor's own count, and wrap the invocation in a `try`/`catch (...)` that logs and swallows so a throwing callback cannot terminate a pool thread (design.md D3); verify all `[task-executor]` cases pass, including a case whose callback throws and asserts the run still completes with that task's outcome intact
- [x] 1.3 Verify the hook introduces no new lock and no second counter: `rg -n "onTaskFinished" src/core/task_executor.cpp` shows exactly one call site, and `rg -n "atomic" src/core/task_executor.cpp` lists the same two atomics as before the change
- [x] 1.4 Verify no caller is migrated yet: `rg -n "onTaskFinished" src` matches only `src/core/task_executor.{h,cpp}` and the test

## 2. The item contract and the runner

- [x] 2.1 Add a direct `runStage` case over a throwaway item type: items whose `alreadyDone` is true are filtered out and counted `skipped`, the rest run and get `outcome.state` written back (`Succeeded`/`Failed` with the error text), `StageResult` counts add up to the input size, and a failing item does not stop the others; verify the case fails to compile (red first)
- [x] 2.2 Add `src/core/media_item.h` with the concept, `ItemState`, `ItemOutcome`, `StageSpec`, `StageResult` and `runStage` per design.md D1/D2/D3/D8; verify the 2.1 case passes
- [x] 2.3 Add a case pinning the bar-text contract (design.md D8): with no `postfix` the bar shows the text the flow seeded it with first and `"{verb}: {done}/{total}"` (plus the unit) after the first completion; with a `postfix` the postfix text **replaces** that seed text entirely, so a stage like organize's shows `"Analyzing"` then `"3/5 - 12 img/s"` and never `"Analyzing: 3/5"`; also pin that the bar survives the stage on both the cancel and the success path, so the flow is what erases it. A `prompt` field on `StageSpec` was drafted and then dropped during implementation, because the flow's own `addBar` call is what carries that text.
- [x] 2.4 Verify the failure print is single-sourced: `rg -n '"  {}: {}"' src` matches only `src/core/media_item.h` (the three per-flow loops at `video_process.cpp:562`, `picture_process.cpp:594`, `picture_video_webp.cpp:300` are gone or, for video, still present and listed as out of scope)

## 3. Picture migrates (the proof)

- [x] 3.1 Migrate the WebP conversion phase (`src/picture/picture_video_webp.cpp:242-322`): build the item list with the existing `id()` (`:39-41`), pass `cacheBackedByState` (`:48-54`) as `alreadyDone`, move the per-item body into `runOne`, delete `ConversionBatchState`; verify `xmake test-report --tag="[picture]"` passes and the conversion bar text is byte-identical
- [x] 3.2 Migrate the compress phase (`src/picture/picture_compress.cpp:275-351`): delete `BatchState`, keep the sequential retry pass (`:191-250`) and its own `"Retrying: 0/N"` bar as a non-`runStage` loop, and keep the "all failed → error" rule; verify `xmake test-report --tag="[picture]"` passes
- [x] 3.3 Add `src/picture/picture_types.h` (a new file) holding the collapsed `MediaItem`, and delete `CompressTask`, `ConversionTask` and `CompressResult` (`picture_compress.h:29-34`); verify `rg -n "CompressResult|ConversionTask|CompressTask" src` returns nothing outside the new header, and the `[picture]` cases pass
- [x] 3.4 Verify the phase-level job-state contract is untouched: `picture_process.cpp` still merges exactly one `makeCompressPhaseTask()` and still calls `markRunning`/`markSucceeded`/`markFailed`/`markInterrupted` on `kCompressPhaseTaskId`; verify the `[picture]` resume cases pass
- [x] 3.5 Verify picture's asserted output did not move: `git diff tests/picture/` changes **no asserted string**, and the one `TEST_CASE` title naming the deleted `CompressTask` was necessarily renamed (construction sites may change, because `CompressTask`/`ConversionTask`/`CompressResult` are collapsed in 3.3) and `xmake test-report --tag="[picture-process]"` passes

## 4. Organize migrates (the existing model)

- [x] 4.1 Give `ImageItem` (`src/organize/organize_types.h:36-43`) the concept members - `id()` from `contentHash`, `label()` from the filename, `source()` as `path`, and `outcome()` over the new `result` field (no `target()`: the Post-Change Review dropped it along with organize's planned-destination field, because the shared layer never reads one - see section 6); verify `xmake build encro` succeeds and the `[organize]` cases pass
- [x] 4.2 Migrate the analysis phase (`src/organize/pipeline.cpp:118-189`) onto `runStage`, deleting the inline counting wrapper (`:155-175`) and the `img/s` postfix becoming the `postfix` callback; verify the `[organize]` cases pass and the `"Analyzing"` bar plus `"n/m - N img/s"` postfix are byte-identical
- [x] 4.3 Verify the content-hash cache still dedupes and the counts still add up: a case where two items share a `contentHash` runs the engine once, the cache hit is filtered by `alreadyDone` and counted in `StageResult::skipped`, the bar's total is the **uncached remainder** (matching `pipeline.cpp:152` today), and the printed `"(N cache hits)"` still comes from organize's own counter (`:107-113`) rather than from `StageResult::skipped`
- [x] 4.4 Verify organize's routing, clustering and copy stages are untouched: `git diff --stat src/organize/cluster.cpp src/organize/assign.cpp src/organize/execute.cpp` is empty

## 5. Verification & commits

- [x] 5.1 Run `xmake test-report` (full unit suite) and confirm zero failures
- [x] 5.2 Run `xmake build e2e_tests && xmake run e2e_tests` and confirm the picture pack/compress flows and the organize flow still pass
- [x] 5.3 Run `xmake test-parallel` and confirm every shard reports its assigned case count
- [x] 5.4 Confirm the out-of-scope flows are untouched: `rg -n "runStage" src` matches no file under `src/video/`, `src/pack/` or `src/preview/`
- [x] 5.5 Run `xmake fmt` before committing and confirm a second run produces no further changes; run `xmake tidy` and confirm no new diagnostics
- [x] 5.6 Commit the planning artifacts first as their own `docs:` commit (proposal, design, tasks, `.openspec.yaml`), then implementation + tests + ticked `tasks.md` in one `refactor:` commit (English, conventional, subject < 72 chars, body wrapped at 80); verify with `git log --oneline -2` that the split is docs-then-refactor

## 6. Post-Change Review follow-up

- [x] Run the `code-review` skill over `c28497f..HEAD` on all three axes, with the change as the spec source.
- [x] Apply the accepted findings: drop `target()` from the concept and organize's dead `targetPath`/`planTargets`; make `printFailures` take a mutable span and delete the const `outcome()` overloads it forced; collapse the conversion's backed/pending split into one pass keyed by item address; delete picture's filtered `readyItems` copy in favour of `isPackable`; make `closeConvertedConversion` return `void`; share `closeCanceledStage`; delete `StageSpec::hideCursor`; trim `media_item.h`'s prose; record the red-first compile errors in the commit body, which task 1.1 specified and the first commit omitted.
- [x] Rejected: `FakeItem` in `tests/media_item_tests.cpp` is not a forbidden mock — the runner is a template over a concept, so testing it requires a type satisfying the concept, and reusing a flow's item type would tie the runner's test to a flow.
- [x] Rejected: `StageResult::total` and `attempted` stay. They are the runner's result contract, design D8 names them, and the runner's own case asserts the accounting invariant that needs them.
- [x] Rejected: extracting a bar handle type for `StageSpec`'s `(progress, barIndex)` pair would add a type without removing either value — `progress.h`'s API takes the index everywhere.
- [x] Accepted gap: neither accepted output change is pinned by a test. Pinning the retry-recovery listing needs a fake tool invocation that fails and then succeeds within one run, which the fake-tool gating cannot express today.
