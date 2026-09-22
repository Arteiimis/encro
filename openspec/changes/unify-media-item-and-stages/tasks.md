## 1. The completion hook (no caller migrated yet)

- [x] 1.1 Add the hook case first: with `maxConcurrency = 1` and a known task list, `onTaskFinished` is called exactly once per task that ran, with `done` reaching `total`, and is **not** called for a slot the stop signal skipped (`testutils::ScopedStopSignalReset`, stop requested from a task body); verify the case fails to compile (red first) and record the compile error in the commit body
- [x] 1.2 Add `std::function<void(std::size_t done, std::size_t total)> onTaskFinished` to `TaskPlan` (`src/core/task_executor.h:28-33`), invoke it from the worker loop after the outcome slot is written, passing the executor's own count, and wrap the invocation in a `try`/`catch (...)` that logs and swallows so a throwing callback cannot terminate a pool thread (design.md D3); verify all `[task-executor]` cases pass, including a case whose callback throws and asserts the run still completes with that task's outcome intact
- [x] 1.3 Verify the hook introduces no new lock and no second counter: `rg -n "onTaskFinished" src/core/task_executor.cpp` shows exactly one call site, and `rg -n "atomic" src/core/task_executor.cpp` lists the same two atomics as before the change
- [x] 1.4 Verify no caller is migrated yet: `rg -n "onTaskFinished" src` matches only `src/core/task_executor.{h,cpp}` and the test

## 2. The item contract and the runner

- [x] 2.1 Add a direct `runStage` case over a throwaway item type: items whose `alreadyDone` is true are filtered out and counted `skipped`, the rest run and get `outcome.state` written back (`Succeeded`/`Failed` with the error text), `StageResult` counts add up to the input size, and a failing item does not stop the others; verify the case fails to compile (red first)
- [x] 2.2 Add `src/core/media_item.h` with the concept, `ItemState`, `ItemOutcome`, `StageSpec`, `StageResult` and `runStage` per design.md D1/D2/D3/D8; verify the 2.1 case passes
- [x] 2.3 Add a case pinning the bar-text contract (design.md D8): with no `postfix` the bar shows `spec.prompt` first and `"{verb}: {done}/{total}"` (plus the unit) after the first completion; with a `postfix` the postfix text **replaces** the prompt entirely, so a stage like organize's shows `"Analyzing"` then `"3/5 - 12 img/s"` and never `"Analyzing: 3/5"`; also pin that `eraseBars` runs on the cancel path as well as the success path
- [x] 2.4 Verify the failure print is single-sourced: `rg -n '"  {}: {}"' src` matches only `src/core/media_item.h` (the three per-flow loops at `video_process.cpp:562`, `picture_process.cpp:594`, `picture_video_webp.cpp:300` are gone or, for video, still present and listed as out of scope)

## 3. Picture migrates (the proof)

- [x] 3.1 Migrate the WebP conversion phase (`src/picture/picture_video_webp.cpp:242-322`): build the item list with the existing `id()` (`:39-41`), pass `cacheBackedByState` (`:48-54`) as `alreadyDone`, move the per-item body into `runOne`, delete `ConversionBatchState`; verify `xmake test-report --tag="[picture]"` passes and the conversion bar text is byte-identical
- [x] 3.2 Migrate the compress phase (`src/picture/picture_compress.cpp:275-351`): delete `BatchState`, keep the sequential retry pass (`:191-250`) and its own `"Retrying: 0/N"` bar as a non-`runStage` loop, and keep the "all failed → error" rule; verify `xmake test-report --tag="[picture]"` passes
- [x] 3.3 Add `src/picture/picture_types.h` (a new file) holding the collapsed `MediaItem`, and delete `CompressTask`, `ConversionTask` and `CompressResult` (`picture_compress.h:29-34`); verify `rg -n "CompressResult|ConversionTask|CompressTask" src` returns nothing outside the new header, and the `[picture]` cases pass
- [x] 3.4 Verify the phase-level job-state contract is untouched: `picture_process.cpp` still merges exactly one `makeCompressPhaseTask()` and still calls `markRunning`/`markSucceeded`/`markFailed`/`markInterrupted` on `kCompressPhaseTaskId`; verify the `[picture]` resume cases pass
- [x] 3.5 Verify picture's asserted output did not move: `git diff tests/picture/` contains **no changes to string literals** (construction sites may change, because `CompressTask`/`ConversionTask`/`CompressResult` are collapsed in 3.3) and `xmake test-report --tag="[picture-process]"` passes

## 4. Organize migrates (the existing model)

- [x] 4.1 Give `ImageItem` (`src/organize/organize_types.h:36-43`) the concept members — `id()` from `contentHash`, `label()` from the filename, `source()` as `path`, `target()` as the planned `organized/<folderName>/<filename>`, `outcome()` — and add the `outcome` field; verify `xmake build encro` succeeds and the `[organize]` cases pass
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
