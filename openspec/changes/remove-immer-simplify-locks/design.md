## Context

See proposal.md for motivation. Current state shaping the design: the only cross-thread uses are (a) the video-info cache (`immer::atom<immer::map>` — written by the scan/probe task pools, read by the monitor and workers) and (b) the active encode slots (`immer::atom<immer::vector<EncodingStatePtr>>` — written by workers on task start/end, read by the monitor every ~20 ms). Everything else (`PendingVidList`, `PendingActionIdList`, `ActionIdMap`, `EncodeResultsMap`) is single-threaded. `EncodingState` already mixes mutex-guarded fields with one atomic (`lastProgressAtomic`); `lastProgress` (mutex-guarded) has no readers.

## Goals / Non-Goals

**Goals:**

- Remove the immer dependency entirely (xmake + code).
- Replace the two real concurrency points with std primitives that are shorter than the current immer wiring, without changing their call signatures.
- Delete the dead `EncodingState::lastProgress` field.
- Keep every externally observable behavior identical: same output text, same file formats, same recommended-job/zip ordering, same performance envelope.

**Non-Goals:**

- No change to `EncodingState::mtx`, `jobstate::Store::mtx_`, `ProgressContext::mtx_`, pack/logging locks — the lock audit found them justified; this change only removes immer and one dead field.
- No lock-free algorithm design, no new synchronization primitives beyond `std::mutex`/`std::shared_mutex`.
- No change to CLI, job-state schema, progress rendering, or encode semantics.
- `picture_compress` results mutex stays (contention is ~zero; per-index slots would be style-only churn).

## Decisions

### D1: Cache — `std::shared_mutex` + `path_map<json::value>` (instead of single mutex)

Reads (`find`) outnumber writes (`set` once per probed file) and the monitor reads during its parse passes; `std::shared_mutex` keeps reads concurrent, and the write path is a single hash insert. The public surface (`set`/`find`/`size`) stays identical, so `app_context_tests.cpp`/`video_info_tests.cpp` need no assertion changes. `find` still returns `std::optional<json::value>` by value (same copy semantics as today).

- Alternatives: single `std::mutex` — simpler but serializes the monitor's reads for no reason; copy-on-write `atomic<shared_ptr<const map>>` — hand-rolled immer, rejected.

### D2: Active slots — `std::mutex` + `std::vector<EncodingStatePtr>`

The vector is sized once at construction (worker count, ≤10) and never resized; `setActive`/`clearActive` lock, assign one element, unlock; `activeStates`/`activeState` lock, copy out. The monitor's 20 ms loop takes one uncontended lock per pass (~tens of ns) — negligible. The `SharedSnapshot` wrapper struct and `makeInitialSnapshot`/lambda-`update` glue go away; `snapshot.load()` call sites collapse to direct vector access.

- Alternatives: `std::vector<std::atomic<EncodingStatePtr>>` — C++26 atomic shared_ptr nuance, more code, rejected; keep immer — the entire point of this change.

### D3: `EncodeResultsMap` stays ordered (`std::map`), `ActionIdMap` becomes `path_map` (unordered)

immer::map iterates in key order. Two users observe iteration order: `printEncodingSummary` (failure list) and `collectEncodedOutputFiles` (zip member order). To preserve output exactly, `EncodeResultsMap` maps to `std::map<fs::path, bool>`. `ActionIdMap` is lookup-only (`find` in `createEncodingState`) and joins the existing `path_map` convention (`plannedOutputFiles`, `probeCqByInput`). `PendingVidList`/`PendingActionIdList` become `std::vector`; `toStdVector` is deleted.

### D4: Dead field removal

Delete `EncodingState.lastProgress` (fields only written: `renderProgress` in `video_encoding_state.cpp`, `finalizeState` in `video_batch_execution.h`, zero readers) and keep `lastProgressAtomic` (the actual read path in `updateOverall`).

### D5: Coverage config cleanup

Remove `immer` from the third-party ignore regex in `plugins/coverage/xmake.lua`.

## Risks / Trade-offs

- [Cache write contention] `shared_mutex` exclusive writes vs immer's lock-free CAS writes → writes are one-per-file at scan/probe time (seconds apart); no measurable impact.
- [Slots lock under monitor] monitor acquires the lock every pass → uncontended mutex ≈ 20 ns against a 20 ms cadence; no impact.
- [Hash/ordered-map divergence] `EncodeResultsMap` ordered vs `ActionIdMap` unordered — a future reader iterating `ActionIdMap` would get unstable order → `ActionIdMap` is only ever queried by key; documented in the header comment.
- [Boost lambda2 on `std::map`/`unordered_map`] range-for/`ranges::count_if` use `std::pair<const K, V>` — `_1->*second` still compiles; no call-site change in `printEncodingSummary`/`hasEncodingFailures`.
- [Regression surface] the change touches hot parallel paths → run `xmake test-report` plus `xmake test-parallel` (real-ffmpeg shards) before commit.

## Migration Plan

Single atomic commit set: implementation + tests + `tasks.md` checkboxes in one commit (planning artifacts committed separately, per repo convention). Rollback = `git revert` of that commit; no state-file or on-disk format compatibility concerns.

## Open Questions

None.
