## Why

The codebase has one process runner (`utils::exec2`), one parallel executor (`taskexec::runTasks`, 10 call sites) and one persistence record (`jobstate::TaskRecord`). It has **no per-item record and no stage abstraction**, so every flow rebuilds both.

The same concept — one input file, its planned output, its archive entry name, its job-state id, its outcome, its failure reason — is expressed as six record types and four association strategies:

| Flow | Per-item record | How state crosses stages |
| --- | --- | --- |
| video | `appctx::EncodingState` (`src/core/app_context.h:72-98`) + **five path-keyed maps** | `plannedOutputFiles` / `actionIds` / `probeCqByInput` / `results` / `failureReasons`, re-`find()`ed per stage |
| picture compress | `CompressTask` / `CompressResult` (`src/picture/picture_compress.h:22-34`) | closure captures `&task` |
| picture webp | `ConversionTask` (`src/picture/picture_video_webp.h:19-23`) | closure captures `&task` |
| pack | `PackEntryInput` / group | `zippedFiles[index]` parallel array |
| preview | `Window` | three parallel vectors indexed together |
| organize | `ImageItem` (`src/organize/organize_types.h:36-43`) | **mutation in place** — the only flow that already has the model |

`EncodingState` is the worst case: it exists only for the duration of one encode (`src/video/video_batch_execution.cpp:117`, released by `clearActive` at `:268`, and on the exception path at `:149`), so nothing carries identity across stages and `applyEncodingStateCommonFields` (`:101-115`) re-derives it from the maps every time.

Stage sequencing is likewise hand-written. `runEncodingTasks` (`video_batch_execution.cpp:567-674`) is a probe `switch` → prompt → rebuild the job → encode; picture is a different imperative function (`src/picture/picture_process.cpp:735-808`); organize is a third (`src/organize/pipeline.cpp:349-390`).

Four costs fall out of this, all measurable:

- **Six pool+bar+counter wrappers** reimplement the same discipline — `EncodingProgressState` (`video_batch_execution.h:84-144`), `ProbeProgress` (`encode_probe.cpp:528-563`), `BatchState` (`picture_compress.cpp:29-36,275-351`), `ConversionBatchState` (`picture_video_webp.cpp:82-92,242-322`), `CompactProgressState` (`pack_service.cpp:83-202`), and an inline block (`organize/pipeline.cpp:149-187`). The `"<verb>: 0/N"` bar shape is written three times identically (`picture_compress.cpp:280`, `picture_video_webp.cpp:249`, `encode_probe.cpp:558`); video's layout is different (`"Overall: {}/{}"` plus one bar per worker slot, `video_batch_execution.h:119,140`) and organize's is a bare verb whose lasting text is a rate (`pipeline.cpp:149,172-175`).
- **Every caller re-wraps every task to count for itself** because `runTasks` offers no completion hook — organize's counting wrapper (`pipeline.cpp:155-175`, including a hand-rolled `img/s` rate), preview's `windowsCompleted` (`preview_process.cpp:425,455-462`), picture's `BatchState::completed`. `docs/backlog.md` ("Batch progress in `runTasks`") already scoped the fix and deferred it until `unify-task-outcome` landed, which it now has.
- **Four failure-map-plus-print loops** (`video_batch_execution.cpp:281,324,451,657` → printed `video_process.cpp:557-567`; `picture_compress.cpp:33,71,230` → printed `picture_process.cpp:593-595`; `picture_video_webp.cpp:91,104,299-301`) and four summary-line builders (`video_process.cpp:545-555`, `encode_probe.cpp:1109-1119`, `picture_process.cpp:380-388`, `:624-631`).
- **Three near-twin picture/pack item types** — `CompressTask{inputPath,outputPath,entryName,originalEntryName}`, `ConversionTask{sourcePath,outputPath,entryName}`, `PackEntryInput` — differing mainly in field names.

Why now: this is the last layer before the flows become a sequence of named stages, and `reuse-hash-and-naming-helpers` has already single-sourced the plan-stage helpers the new seam will call.

## What Changes

- **`TaskPlan::onTaskFinished(done, total)`** — the optional completion hook `docs/backlog.md` specified: "so the three sites that wrap every task or carry their own atomic read one counter from the executor instead". The executor owns the count; the caller keeps its percentage formula, because the five sites count five different units (tasks, a 0-85 window, `kStepsPerProbePoint`, files inside one archive, encoded frames) and `docs/backlog.md` records that the executor "cannot own a unit it is never told".
- **One item contract**, `src/core/media_item.h`: a `mediaitem::Item` concept (`id()`, `label()`, `source()`, `outcome()`) plus `ItemState` / `ItemOutcome`. It is a concept, **not a shared struct**: video needs `chosenCq`/`totalFrames`, organize needs `contentHash`/`analysis`/`folderName`, picture needs `entryName`/`originalEntryName`, so one concrete type would need a variant or inheritance and would trade duplication for indirection. The item does **not** carry a `jobstate::TaskRecord` — picture's compress phase persists one phase-level task (`picture_process.cpp:579-587`) and organize persists none, so two of four flows would carry a record they never write.
- **One stage runner**, `mediaitem::runStage(StageSpec, items, ctx)`: filter by the flow's own skip predicate → build `TaskSpec`s → `runTasks` → write `outcome`/failure back onto the items → drive the progress counter. This is where the six wrappers and four failure loops collapse into one each; the summary sentence stays per-flow, because the four sites differ in prose and already share `terminal::summaryCounts`.
- **`CompressTask` and `ConversionTask` collapse into one `MediaItem`** for the picture flow; `PackEntryInput` stays pack's own type (pack is a module, see non-goals) and is built from items at the boundary, as it already is (`picture_process.cpp:262-315`).
- **Picture and organize migrate to the runner** as the proofs. Picture covers both phases (`picture_compress.cpp:275-351`, `picture_video_webp.cpp:242-322`); organize covers the flow that already had the model, so its migration is mostly renaming plus giving `ImageItem` a `TaskRecord`.
- **`skip-if-done` becomes one named seam, `alreadyDone(item)`, with each flow's current predicate preserved.** The four path-driven checks differ today (`jobState::needsExecution` `job_state.cpp:643`, `jobState::actionTargetExists` `:647`, `cacheBackedByState` `picture_video_webp.cpp:48-54`, picture's mtime check `picture_process.cpp:209-212`); unifying the *semantics* changes resume behaviour and is deferred (see non-goals).
- **No intended behaviour change in any command**: identical planned names, identical bar text and ordering, identical summary lines, identical exit codes and identical resume decisions. Two output changes are accepted and recorded in design.md's Risks: converted clips enter the archive in input order, and a retry-recovered compression no longer appears in the failure listing.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None — the item contract and the stage runner are internal; every user-visible string, file set, exit code and resume decision is preserved. `skip_specs: true` is set in `.openspec.yaml` per the precedent of `unify-task-outcome`, `unify-run-teardown` and `unify-window-measurement`.

## Impact

- New: `src/core/media_item.h` (concept, `ItemOutcome`, `StageSpec`, `StageResult`, `runStage`) and `src/picture/picture_types.h` (the collapsed `MediaItem`) — both header-only
- `src/core/task_executor.{h,cpp}` — `TaskPlan::onTaskFinished` and the count it feeds
- `src/picture/picture_process.cpp`, `picture_compress.{h,cpp}`, `picture_video_webp.{h,cpp}` — one item type, two phases on the runner
- `src/organize/organize_types.h`, `pipeline.cpp`, `execute.cpp` — `ImageItem` satisfies the concept, analysis phase on the runner
- `src/core/job_state.h` — **unchanged**; per-item identity comes from `item.id()` and each flow sets its `TaskRecord::id` from it, as `picture_video_webp.cpp:39-41` already does
- Tests: `tests/picture/*`, `tests/organize/*`, `tests/task_executor_tests.cpp` gain cases for the runner and the hook; existing cases stay green unmodified except where they construct a type that was renamed

**Explicitly out of scope:**

- **Video's migration** (`migrate-video-to-media-items`) — collapsing its five path-keyed maps and making `EncodingState` the item touches the encode runner, the progress-file monitor thread (`video_encoding_state.cpp:218-262`) and segment resume (`video_encode_runner.cpp:572-578`), a different risk class from picture and organize. This change must nonetheless not block it: design.md states the five-map mapping explicitly.
- **Unifying resume/skip semantics** — investigated and **rejected**: the four path-driven predicates answer different questions and are deliberately distinct, not duplicated. `jobstate::needsExecution` asks a state question (`job_state.cpp:643`), `jobstate::actionTargetExists` an artifact question (`:647`), `cacheBackedByState` a *safety* question — it requires the saved state to back the cached artifact, because the merge restores any Pending/Interrupted task whose target exists and a stale file in a shared cache directory must never be packed (`picture_video_webp.cpp:44-54`) — and picture compress's mtime check a freshness question for a per-run temp directory (`picture_process.cpp:209-212`). Folding them into one predicate would either loosen the WebP cache guard or tighten the other three, so this change only introduces the per-flow seam. See design.md D4.
- **Pack's grouping** — N items to M archives is a cardinality change and a global partition (`pack.cpp:220-262`); pack stays a module and does not become a stage.
- **Giving organize path-based resume** — a feature, not a refactor. `AnalysisCache` is content-hash dedupe (`pipeline.cpp:107`) and is orthogonal to path progress, so organize needs both; adding the second is its own change.
- **Preview's migration** — its windows are not files and it has no job-state or pack; it is the natural first user of the runner *after* the abstraction is proven, not a proof of it.
