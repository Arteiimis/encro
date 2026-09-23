## Context

See `proposal.md` — Why for the five maps and the two bundle types. Read `unify-media-item-and-stages/design.md` D1-D3 and D8 first: this change consumes that item contract and `runStage`, and adds no new abstraction of its own.

Constraints that shape the approach:

- `appctx::EncodingState` (`src/core/app_context.h:72-98`) holds `std::atomic<float> lastProgressAtomic` and `std::mutex mtx`, so it is neither copyable nor movable. The codebase already works around this with `EncodingStateList = std::vector<EncodingStatePtr>` (`app_context.h:100-101`).
- The monitor thread reads the state through those fields while an encode runs (`video_encoding_state.cpp:218-262`, sampling `lastProgressAtomic` and the progress file).
- `picture_video_webp.cpp:124` constructs an `EncodingState` locally to pass to `encodeVideo` (`:143`) and uses none of its identity fields.
- `planVideoOutputFiles` has 12 `TEST_CASE`s in `tests/video/video_output_planning_tests.cpp` (14 leaf cases, one case holding three `SECTION`s) and one production caller (`video_process.cpp:313`).
- Video's progress layout is two-tier: an "Overall: i/N" bar gated by `progress::showsOverallBar` plus one bar per worker slot gated by `showsSlotBars` (`video_batch_execution.h:117-140`, `core/progress.h:35-36`).
- `runEncodingTasks` has a parallel body (`video_batch_execution.cpp:567-674`) and a sequential body for verbose mode (`:277-340`) that duplicate the per-file bookkeeping.

## Goals / Non-Goals

**Goals:**

- One per-item record for video, alive from planning to summary, replacing five path-keyed maps and two bundle types.
- The per-file bookkeeping written once instead of twice (parallel and verbose-sequential paths).
- `EncodingState` stops living in `app_context.h` without becoming video-private.
- Identical console output, job-state records, planned paths and segment-resume behaviour.

**Non-Goals:**

- Deleting `EncodingProgressState` or changing the slot-bar layout.
- Touching the monitor thread, its sampling contract, or `EncodingState`'s atomics/mutex.
- Changing `planVideoOutputFiles`' signature.
- Migrating the probe phase, `video_info.cpp`'s helper batches, pack or preview.

## Decisions

### D1: `EncodingState` becomes the item by gaining one field, not by being wrapped

It already carries `inputPath` (the key all five maps share), `actionId`, `plannedOutputFile` and `chosenCq` — so three of the five maps' values (`plannedOutputFiles`, `actionIds`, `probeCqByInput`) are already fields under other names. The genuinely new state is **one** field, `mediaitem::ItemOutcome outcome`, which absorbs the last two maps (`results` → `outcome.state`, `failureReasons` → `outcome.failureReason`). `id()`, `label()`, `source()` and `outcome()` are accessors over the existing fields, while `plannedOutputFile` stays a plain field: the concept carries no `target()` (`media_item.h:50-56`), because the shared layer never reads a planned output.

A wrapper (`struct VideoItem { EncodingStatePtr state; ItemOutcome outcome; }`) was considered and rejected: it would add a second type and an indirection to avoid one field, and every existing `EncodingState` user (`video_encode_runner`, `video_encoding_state`, the tests) would keep working on the inner object anyway.

### D2: The item vector is `std::vector<EncodingStatePtr>`, and `runStage` gains a pointer overload

`EncodingState` is non-movable (D-context), so a value vector is not available without splitting the runtime block, which is a non-goal. `appctx::EncodingStateList` is already exactly `std::vector<EncodingStatePtr>`, so this is the type the flow already uses.

`runStage` (from `unify-media-item-and-stages`) is specified over `std::span<Ty>` with the concept checked on `Ty`. This change adds one overload for a span of pointers whose concept is checked on the pointee, and which forwards by dereferencing. Two overloads of a header-only template, no accessor plumbing.

Alternative considered: make the item concept accept both by dereferencing internally — rejected, it hides whether an element is a pointer and makes the concept's error messages worse. Alternative considered: split the runtime block (`lastProgressAtomic`, `mtx`, progress-file fields) into a short-lived `EncodeRunState` so the item becomes a plain value — rejected as a non-goal-sized change to the monitor's contract; recorded here so a later reader knows it was seen.

### D3: The maps die as *cross-stage carriers*; the plan function keeps its shape

`planVideoOutputFiles` still returns `eh::Result<path_map<fs::path>>` and still owns `ensureUniqueOutputPaths`. The flow folds that map into the items immediately after planning, and from that point the items are the only carrier:

| Stage | Reads | Writes |
| --- | --- | --- |
| plan | — | `source`, `plannedOutputFile`, `id` |
| probe | `source` | `chosenCq` (skip decisions stay in the probe's own plan list) |
| encode | `source`, `plannedOutputFile`, `id`, `chosenCq` | `outcome`, `outputFile` |
| summary | all | — |
| pack input collection | `outcome`, `plannedOutputFile` | — |

The planned output is the item's own field, not a concept member: `unify-media-item-and-stages`' Post-Change Review dropped `target()` along with organize's dead destination field, because the shared layer never reads a planned path, so the table above names `plannedOutputFile` (`media_item.h:287`) where a draft named `target`. The pack-input collection reads that same field (`video_process.cpp:411-415`), not `outputFile`.

`EncodingBatchJob` dissolves into the item vector; `EncodingBatchOutcome` dissolves into the item vector plus the non-per-item values it also carried (`attentionWarnings`, `dryRun`, `skippedCount`, `encodeElapsed`, and the `canceled` flag that `results == nullopt` used to signal), which stay in `EncodingBatchSummary` because they are not per-item (`video_batch_execution.h:19-33`).

Alternative considered: change the planner to annotate items directly — rejected; it forces 13 test rewrites and removes no duplication, because the planner's map is a plan-stage local, not a carried map.

### D4: The verbose-sequential path is the same stage with `maxConcurrency = 1` and no bar

`runEncodingWithoutProgress` (`:277-340`) duplicates the per-file bookkeeping of the parallel path. Both become one `runStage` call; the verbose path passes `maxConcurrency = 1`, `progress = nullptr` and keeps its own log lines. This is the change's second-largest line saving and the one that removes a divergence risk (the two bodies currently update job-state and the failure map by hand in two places).

The verbose path keeps its `"Echo enabled: progress bars disabled."` notice and its `LOG_*` lines byte-identical; only the bookkeeping moves.

### D5: The slot-bar layout stays with the flow

`EncodingProgressState` is passed as the stage's own `ProgressContext` and the stage runs in the "caller draws its own bar" mode from `unify-media-item-and-stages` D8. `runStage` still supplies the bookkeeping: filter, `TaskSpec` build, `outcome` write-back, failure collection, counts.

The count reaches the flow through the hook `runStage` installs on `TaskPlan` (`media_item.h:163`) — but the bar's *value* must not: `StageSpec::setBarProgress = false` (`video_batch_execution.cpp:573`) keeps the hook off the progress value while still calling the flow's `postfix` (`:575-580`, `markFinished`/`updateOverall`/`overallText`). The runner's `done/total` fraction is not this stage's fraction: the Overall bar's denominator is every planned item (`video_batch_execution.h:154`) and it adds the probe-skipped items plus each running encode's partial progress (`:242-270`). Feeding it would also sample the bar's ETA estimator with a count fraction, which is not the quantity that estimator projects (`progress.cpp:369-370`).

`setBarProgress` is the one `StageSpec` field this change added, defaulting to true (`media_item.h:65-69`), so picture and organize are untouched. `runStage`'s `hideCursor` stopped being a constant: it is `spec.progress != nullptr` (`media_item.h:160-162`), because a stage that draws nothing must not hide the cursor — video's verbose path is that case — while a stage that draws a bar owns it.

The hook calls `postfix` before the null-progress early return (`media_item.h:167-170`), so the count still flows when the Overall bar is absent: the compact single-item run, where `showsOverallBar(1,1,true)` is false (`progress.cpp:48-50`) while the slot bar is still drawn (`:52-54`). The flow wraps that case in its own `progress::CursorGuard` (`video_batch_execution.cpp:564-567`).

Consequence to accept: the per-item encode task no longer calls `markFinished`/`updateOverall`, and outside the monitor thread their only caller is the hook's `postfix` (`video_batch_execution.cpp:575-580`), so the flow's count mirrors the runner's `done` instead of being incremented by each task. `EncodingProgressState` still owns the bar math (`barEncodingStart`/`barEncodingStatus`/`barDone`, `overallText`, `updateOverall`; `video_batch_execution.h:154,192-270`).

### D6: Job-state writes move onto the item's identity, not the map

`markRunning` / `markProgress` / `markSucceeded` / `markFailed` keyed off `vidState.actionId` looked up from `actionIds`; they now read the item's own `actionId` under the same guard (`video_batch_execution.cpp:71-72`, `:105-114`, `:135-141`), not literally `id()` — which derives an `encode:<path>` form when the field is unset (`media_item.h:317-320`) and would therefore add a store call exactly where the old map lookup found nothing. The ids themselves are unchanged — populated from the same `prepareEncodeActions` output (`video_process.cpp:92-102`), which already builds `jobstate::makeEncodeTask(...).id`.

`persistedElapsedMs` (`video_batch_execution.cpp:40-49`) and the segment reads (`video_encode_runner.cpp:565-575`, which derive that same `taskId`) also key off the item's identity; no record shape or fingerprint changes, so an in-flight state file from before this change resumes identically.

### D7: Output order is reproduced by an explicit sort, with the map's own comparison

`EncodeResultsMap` is a `std::map`, not an unordered map, and its comment says why: *"Ordered result map (path-sorted iteration) — the failure list and zip member order in the summary/output collection depend on this ordering"* (`video_batch_execution.h:24-26`). Two consumers rely on it:

- the failure list printed by `printEncodingSummary` (`video_process.cpp:558-567`), which iterates it
- `collectEncodedOutputFiles` (`:401-414`), which builds the pack file list — so the **archive member order** is a persisted artefact of the run

`runStage` iterates items in input order and never sorts (`unify-media-item-and-stages` D2). So the summary and the pack-input collection must iterate a **path-sorted view of the items** rather than the item vector.

The comparison matters and is not `naming::stablePathString`: `std::map<fs::path, bool>` orders by `fs::path::operator<`, which compares the native form literally, while `stablePathString` lowercases and normalises separators (`core/collision_naming.h:18-24`). Paths differing only in case would order differently, and the failure list and archive members would silently change for those inputs. The explicit sort therefore uses `fs::path::operator<`, and a case with case-differing paths pins it — no existing test covers that, which is why the order survived unnoticed.

Alternative considered: keep the items in a sorted container instead of a vector — rejected, it would force the sort on every stage rather than only the two consumers, and it makes the item's position depend on a path that a stage may legitimately rename.

## Risks / Trade-offs

- **The largest single edit in the three structural changes.** → D3 keeps the planner out, D5 keeps the progress object out, and D6 changes no ids; each of those removes a class of edit from the diff. If review judges the diff too large, the split point is "maps die" (D3+D6) before "verbose path merges" (D4).
- **`runStage` gaining a pointer overload widens the abstraction for one caller.** → it is two overloads in a header-only template and the alternative (a value vector) is blocked by a non-goal. If video were the only pointer user, the honest fix would be the runtime-block split; that is recorded as the upgrade path.
- **The verbose path's log lines are asserted by tests.** → `tests/video/video_process_orchestration_tests.cpp` covers the verbose narration; those cases' asserted strings must not change. It never built `EncodingBatchJob`/`EncodingBatchOutcome`, so this change added a case there and edited no construction site.
- **Failure-list and archive-member order can silently change (D7).** → the sort uses `fs::path::operator<` (the comparison the map used), not `stablePathString`, and a new case with case-differing paths pins both the failure list and the archive member order. No existing case covers it.
- **`EncodingState` leaving `app_context.h` touches picture.** → `picture_video_webp.cpp:124` is the only non-video construction; it changes one include and one qualified name. Picture's behaviour is untouched, and `unify-media-item-and-stages` has already migrated that file.
- **An in-flight `.encro` state file must still resume.** → D6 changes no id and no fingerprint; `tests/job_state_tests.cpp` and the video resume e2e cases are the guard.
- **Mixed codebase after this change**: picture and organize are on `runStage`, pack and preview are not. → preview is the named next user; pack is out by design.
- **Accepted output delta: a verbose failure with no encoder error prints no summary line.** `printEncodingSummary` used to fall back to a bare `"  <path>"` line when `failureReasons` had no entry for the failed path; the shared `printFailures` skips an empty `outcome.failureReason` (`media_item.h:242-250`), the rule picture already adopted (`picture_process.cpp:582`, `picture_video_webp.cpp:169`). Accepted: the file is still counted as failed, the exit code is unchanged, and a failure the encoder explained prints as before, in path order — the reason-less line is the only thing gone.
- **Accepted fix: `summaryOutputDir` is now deterministic.** It no longer reads an `unordered_map`'s `begin()`: it walks the item vector, which is in input order, and takes the first item with a planned output (`video_process.cpp:247-260`). A flat run prints the same directory; a multi-directory one names the first input's directory instead of an arbitrary element. This closes the "`summaryOutputDir` names an arbitrary directory from an unordered map" entry in `docs/backlog.md`.
- **Accepted delta: in verbose mode an exception escaping the encoder becomes a per-item failure.** The verbose path now runs through `runStage` → `runTasks`, whose per-task `try`/`catch` records the message and carries on (`task_executor.cpp:52-62`); the old sequential loop let the throw propagate out of the encode phase. This is what the parallel path already did, so the two modes agree.
- **Accepted delta: `noteStopRequest`'s in-loop call is gone from the verbose path.** The old sequential loop recorded the stop request itself and `break`ed; the recording now happens at the drain after the encode phase — `maybeHandleInterruptedEncoding` calls `requestCancel()` and `markIncompleteInterrupted(pendingActionIds)` right after `runEncodingTasks` (`video_process.cpp:230-243`, called at `:361`) for the verbose and parallel paths alike. Nothing visible or resumable is lost; only the moment of recording moves.
- **A latent test bug fixed on the way (not a behaviour delta).** The case meant to exercise a skip-encode plan used a 3 MiB source against a 10 MiB probe estimate, so both its files were probe-skipped and its assertions (the skipped path absent from the results and from the encode log) held vacuously; the fixture is now 24 MiB (`tests/video/video_batch_execution_tests.cpp:613-618`), which survives the filter and lets the case assert the mixed skip/encode plan it names.

## Migration Plan

1. Move `EncodingState` / `EncodingStatePtr` / `EncodingStateList` from `src/core/app_context.h` to `src/core/media_item.h`; update the two flows' includes; build and run the video and picture suites (no behaviour change yet).
2. Add `outcome` and the concept accessors; add the pointer overload to `runStage` with a direct test.
3. Fold `plannedOutputFiles`/`actionIds`/`probeCqByInput` into the items at their production sites (`video_process.cpp:313`, `:96-136`, `runProbeStage`) and delete those three maps.
4. Fold `results`/`failureReasons` into `outcome`; delete `collectEncodingResults` (`:440-455`) and the print loop in `video_process.cpp:557-567`.
5. Route the encode phase through `runStage`; delete `EncodingBatchJob`/`EncodingBatchOutcome`; update the two test files that construct them.
6. Merge the verbose-sequential path into the same stage (D4).
7. Full unit suite, e2e (including the stop/resume flows), `test-parallel`, `fmt`, `tidy`.

Rollback: steps 1-4 are additive and revertable independently of 5-6, which are the behaviour-visible ones.

## Open Questions

None. The decision that could have changed the task breakdown — whether the item is a value or a pointer — is settled in D2 with its upgrade path recorded.
