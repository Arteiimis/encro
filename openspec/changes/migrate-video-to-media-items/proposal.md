## Why

Video is the flow with the most machinery and the least per-item identity. `appctx::EncodingState` (`src/core/app_context.h:72-98`) is created when an encode starts and released when it ends (`video_batch_execution.cpp:117`, `clearActive` at `:268` and on the exception path at `:149`), so nothing about an item survives from planning to summary. Identity is instead re-derived at every stage boundary by `applyEncodingStateCommonFields` (`video_batch_execution.cpp:101-115`) reading five separate path-keyed maps:

| Map | Produced by | Consumed by | Lives on the item as |
| --- | --- | --- | --- |
| `plannedOutputFiles` `path_map<fs::path>` | `planVideoOutputFiles` (`video_process.cpp:313`) | `runEncodingTasks`, `collectEncodedOutputFiles` (`:401`) | `plannedOutputFile` |
| `actionIds` `path_map<std::string>` | `prepareEncodeActions` (`:96`) | `applyEncodingStateCommonFields` (`:105`) | `id` |
| `probeCqByInput` `path_map<int>` | `runProbeStage` (`:385`) | `applyEncodingStateCommonFields` (`:109`) | `chosenCq` |
| `results` `map<fs::path,bool>` | `collectEncodingResults` (`:440-455`) | summary (`video_process.cpp:519-598`), pack input collection | `outcome.state` |
| `failureReasons` `map<fs::path,std::string>` | three sites (`video_batch_execution.cpp:281,324,451`) | one print loop (`video_process.cpp:557-567`) | `outcome.failureReason` |

`EncodingBatchJob` (`video_batch_execution.h:46-50`) exists only to carry three of those maps as one argument, and `EncodingBatchOutcome` (`:28-41`) only to carry the results back out.

The same shape is written twice: `runEncodingTasks` (parallel, `:567-674`) and `runEncodingWithoutProgress` (sequential, verbose mode, `:277-340`) both rebuild a per-file state, call `encodeVideo`, and separately update job-state and a failure map.

Why now: `unify-media-item-and-stages` provides the item contract and `runStage`; video is the flow that pays for them most, but its migration touches the progress-file monitor thread, segment resume and the probe handoff, which is why it is separate from the picture/organize proof.

## What Changes

- **`EncodingState` becomes video's `mediaitem::Item`.** It gains a `result` field of type `mediaitem::ItemOutcome`, read through the `outcome()` accessor the concept requires (a field and a method cannot share a name), and that accessor takes over the two map roles it does not already hold (`results` → `outcome.state`, `failureReasons` → `outcome.failureReason`); the other three are already fields under different names (`plannedOutputFiles` → `plannedOutputFile`, `actionIds` → `actionId`, `probeCqByInput` → `chosenCq`), so they become direct reads instead of map lookups. It then satisfies the concept from `unify-media-item-and-stages`.
- **It moves out of `src/core/app_context.h`** into `src/core/encoding_state.h`, beside the contract (which stays free of it — `media_item.h` does not include the new header). It is not video-private — `picture_video_webp.cpp:124` constructs one to pass to `encodeVideo` — so it cannot move under `src/video/`; but `AppConfig`/`RuntimeContext` should not be the home of a 27-field per-item record either.
- **The five maps and the two bundle types are deleted.** `EncodingBatchJob` and `EncodingBatchOutcome` dissolve; the flow passes `std::vector<EncodingStatePtr>` (`appctx::EncodingStateList`, already the type in use) and reads outcomes off the items.
- **The encode phase runs through `runStage`** in its "caller draws its own bar" mode, keeping `EncodingProgressState`'s slot layout untouched (`video_batch_execution.h:84-144`).
- **The sequential verbose path (`:277-340`) is expressed as the same stage** executed with `maxConcurrency = 1` and no bar, so the per-file bookkeeping exists once instead of twice.
- **`planVideoOutputFiles` keeps its signature and its 12 cases.** The plan produced by that function is folded into the items by the flow, so only the *cross-stage* carrier disappears. Rewriting the planner to annotate items would force the planner's test file to be rewritten for no reduction in duplication.
- **Output order is preserved explicitly.** The path-sorted order that two persisted artefacts depend on — the failure list and the archive member order — is reproduced by an explicit sort with the same comparison the map used, rather than inherited from a `std::map`. See design.md D7; this is also why `unify-media-item-and-stages` D2 forbids a stage from relying on an order the runner does not produce.
- **No *intended* behaviour change**: identical planned paths, identical bar text, identical exit codes, identical job-state records and segment-resume decisions. Four deltas were produced anyway and are accepted; design.md's Risks names each one.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None — this is an internal reshape of video's per-item state. Every user-visible and persisted artifact is preserved: the same `jobstate::TaskRecord` ids and statuses, the same `.encro` segment directories, the same console output. `skip_specs: true` is set in `.openspec.yaml` per the precedent of `unify-task-outcome` and `unify-run-teardown`.

## Impact

- `src/core/app_context.h` — `EncodingState` / `EncodingStatePtr` / `EncodingStateList` move out into `src/core/encoding_state.h`; both flows' includes change
- `src/video/video_batch_execution.{h,cpp}` — the largest edit: three maps of bookkeeping, `EncodingBatchJob`/`EncodingBatchOutcome` and the parallel/sequential duplicate collapse onto the item vector and `runStage`
- `src/video/video_process.cpp` — the encode entry, the summary and `collectEncodedOutputFiles` read items instead of the results map; the failure print loop moves to the runner
- `src/video/video_encode_runner.{h,cpp}`, `video_encoding_state.cpp` — signature-only changes where `EncodingState` is used; the monitor thread's contract is untouched
- Tests: `tests/video/video_batch_execution_tests.cpp` and `tests/video/video_process_orchestration_tests.cpp` are updated where they build the batch types, and `tests/video/encode_probe_tests.cpp` where its scaffold builds both types; `tests/video/video_output_planning_tests.cpp` is **not** touched; `tests/video/video_encode_runner_tests.cpp` changes only where it constructs an `EncodingState`

**Depends on:** `unify-media-item-and-stages` (`runStage`, the concept) and `reuse-hash-and-naming-helpers` (which edits `planVideoOutputFiles`, the function this change deliberately leaves in place).

**Explicitly out of scope:**

- **The slot-bar layout** — `EncodingProgressState` stays. Video draws one "Overall: i/N" bar plus one bar per worker slot; the runner's single-bar shape does not express that, and teaching it to would be an abstraction for one flow.
- **The progress-file monitor thread** (`video_encoding_state.cpp:218-262`) and `EncodingState`'s `mtx`/`lastProgressAtomic`. Splitting the runtime block off the item would make the item a movable value but forces a change to the monitor's contract; not worth it here.
- **Segment resume** (`video_encode_runner.cpp:572-578`, `markSegmentProgress` `:417-428`) and the `segmentIndex` it writes onto the task record (`jobstate::TaskRecord`, `src/core/job_state.h:66`).
- **The probe phase** — `encode_probe.cpp` keeps its own pool, its nested base-CQ pool (`:441`), its bars and its probe cache; only the CQ values it produces move onto the items.
- **`video_info.cpp`'s two `runTasks` uses** (`:236,281`) — HEVC filtering and WebP prewarming are helper batches with no per-item lifecycle; they are not stages.
- **Resume skip semantics** — the predicates stay as they are; see `unify-media-item-and-stages/design.md` D4 for why they are deliberately distinct rather than duplicated.
