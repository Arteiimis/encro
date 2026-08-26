## Context

See proposal.md for motivation. Current state shaping the design: the only cross-thread uses are (a) the video-info cache (`immer::atom<immer::map>` — written by the scan/probe task pools, read from the scan/preview/encode paths) and (b) the active encode slots (`immer::atom<immer::vector<EncodingStatePtr>>` — written by workers on task start/end, read by the monitor every ~20 ms). Everything else (`PendingVidList`, `PendingActionIdList`, `ActionIdMap`, `EncodeResultsMap`) has no write contention: built once, then read-only (or purely single-threaded). `EncodingState` already mixes mutex-guarded fields with one atomic (`lastProgressAtomic`); `lastProgress` (mutex-guarded) has no readers.

## Goals / Non-Goals

**Goals:**

- Remove the immer dependency entirely (xmake + code).
- Replace the two real concurrency points with std primitives that are shorter than the current immer wiring, without changing their call signatures.
- Delete the dead `EncodingState::lastProgress` field.
- Keep every user-visible behavior identical *except* iteration order of failure lists and zip members, which becomes deterministic path-sorted order (see D3): same output text, same file formats, same performance envelope.

**Non-Goals:**

- No change to `EncodingState::mtx`, `jobstate::Store::mtx_`, `ProgressContext::mtx_`, pack/logging locks — the lock audit found them justified; this change only removes immer and one dead field.
- No lock-free algorithm design, no new synchronization primitives beyond `std::mutex`/`std::shared_mutex`.
- No change to CLI, job-state schema, progress rendering, or encode semantics.
- `picture_compress` results mutex stays (contention is ~zero; per-index slots would be style-only churn).

## Decisions

### D1: Cache — `std::shared_mutex` + `path_map<json::value>`

Writes happen once per probed file from the scan/probe task pools; reads come from `getVidTotalFrames`/`loadCachedOrProbeVideoInfo`, which run on the main thread and in the parallel preview scoring windows (`preview_process.cpp`), plus the monitor indirectly. `std::shared_mutex` keeps concurrent readers from blocking each other, and the write path is a single hash insert. The public surface (`set`/`find`/`size`) stays identical, so `app_context_tests.cpp`/`video_info_tests.cpp` need no assertion changes. `find` still returns `std::optional<json::value>` by value (same copy semantics as today). The unused `load()` accessor is deleted with the wrapper.

- Alternatives: single `std::mutex` — sufficient if reads were proven single-threaded (contention would be ~zero; acceptable fallback); copy-on-write `atomic<shared_ptr<const map>>` — hand-rolled immer, rejected.

### D2: Active slots — `std::mutex` + `std::vector<EncodingStatePtr>`

The vector is sized once at construction (worker count, ≤10) and never resized; `setActive`/`clearActive` lock, assign one element, unlock; `activeStates`/`activeState` lock, copy out. The monitor's 20 ms loop takes one uncontended lock per pass (~tens of ns) — negligible. The `SharedSnapshot` wrapper struct and `makeInitialSnapshot`/lambda-`update` glue go away; the `EncodingExecutionContext::loadShared` helper (which returns `shared_ptr<const SharedSnapshot>`) cannot keep its return type and is deleted with it (no external callers).

- Alternatives: `std::vector<std::atomic<EncodingStatePtr>>` — C++26 atomic shared_ptr nuance, more code, rejected; keep immer — the entire point of this change.

### D3: `EncodeResultsMap` becomes ordered `std::map`; `ActionIdMap` becomes `path_map` (unordered)

`immer::map` (v0.9.1) is a HAMT — its iteration order is hash-based and unspecified, not key-sorted. Two places expose iteration order to the user: `printEncodingSummary` (failure list) and `collectEncodedOutputFiles` (zip member order). Replacing it with `std::map<fs::path, bool>` changes that order from hash-order to deterministic path-sorted order — a behavior change, but a strictly more predictable one; e2e asserts only the `"Failed to encode: N"` count line, never the ordering. (An `unordered_map` would have been no more faithful — it would simply swap one unspecified order for another.) `ActionIdMap` is lookup-only (`find` in `createEncodingState`) and joins the existing `path_map` convention (`plannedOutputFiles`, `probeCqByInput`). `PendingVidList`/`PendingActionIdList` become `std::vector`; `toStdVector` is deleted. The `videobatch::ActionIdMap`/`EncodeResultsMap` aliases are kept (re-pointed at the std containers) so `video_batch_execution_tests.cpp`/`encode_probe_tests.cpp` compile unchanged.

### D4: Dead field removal

Delete `EncodingState.lastProgress` (fields only written: `renderProgress` in `video_encoding_state.cpp`, `finalizeState` in `video_batch_execution.h`, zero readers) and keep `lastProgressAtomic` (the actual read path in `updateOverall`).

### D5: Coverage config cleanup

Remove `immer` from the third-party ignore regex **and the accompanying comment** in `plugins/coverage/xmake.lua` (the comment text would otherwise trip the no-reference check).

## Risks / Trade-offs

- [Cache write contention] `shared_mutex` exclusive writes vs immer's lock-free CAS writes → writes are one-per-file at scan/probe time (seconds apart); no measurable impact.
- [Slots lock under monitor] monitor acquires the lock every pass → uncontended mutex ≈ 20 ns against a 20 ms cadence; no impact.
- [Iteration-order change (D3)] failure list and zip member order change from unspecified hash-order to path-sorted → accepted, documented in proposal; no test asserts ordering.
- [Hash/ordered-map divergence] `EncodeResultsMap` ordered vs `ActionIdMap` unordered — a future reader iterating `ActionIdMap` would get unstable order → `ActionIdMap` is only ever queried by key; documented in the header comment.
- [Boost lambda2 on `std::map`/`unordered_map`] range-for/`ranges::count_if` use `std::pair<const K, V>` — `_1->*second` still compiles; no call-site change in `printEncodingSummary`/`hasEncodingFailures`.
- [Regression surface] the change touches hot parallel paths → run `xmake test-report` plus `xmake test-parallel` (real-ffmpeg shards) before commit.

## Migration Plan

Single atomic commit set: implementation + tests + `tasks.md` checkboxes in one commit (planning artifacts committed separately, per repo convention). Rollback = `git revert` of that commit; no state-file or on-disk format compatibility concerns.

## Open Questions

None.
