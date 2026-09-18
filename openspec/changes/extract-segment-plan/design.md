## Context

See `proposal.md` — Why for the pain and the trigger. The current state that shapes the approach:

- The entry direction is inline in `runSegmentedEncoding` (`src/video/video_encode_runner.cpp:587-751`): `task->segmentIndex` (`:603-605`), `parseSegmentList(listPath)` (`:614`), `reusableSegments` (`:398`), `segmentTotal = ceil(duration / kSegmentDurationUs)` (`:628`), `resumeUs = completed * kSegmentDurationUs` (`:630`), `encodeComplete` (`:414`), `segmentBaseFrameOffset` (`:671`).
- The exit direction is `SegmentListWatch` + `markCompletedSegments` (`:369-391`); the poll loop (`watchSegmentList`, `:426-432`) calls it, and it recomputes `startNumber + entries.size()` and multiplies by `kSegmentDurationUs` before calling `Store::markSegmentProgress`.
- `SegmentSeries` (`src/video/encode_config.h:35-43`) already carries `{segmentDir, startNumber, resumeUs}` into the ffmpeg command line; `segmentBaseFrameOffset` (`src/video/video_progress_parser.h:37`) already converts a resume point to frames; `parseSegmentList` is already tested (`tests/video/video_progress_parser_tests.cpp:219-284`).
- `videoseg` (`src/video/segment_dir.h`) is included only by the runner and its own test; `Task::segmentIndex` has exactly one production consumer of the recorded count (`video_encode_runner.cpp:603-605`).
- E2E coverage of the path is thick and uses the process boundary: `tests/e2e/encro_e2e_tests.cpp:1345-1552` (interrupted attempts, deleted lists, concat-only reruns) plus segment-index assertions at `:1394`, `:1418`, `:1469`, `:2113`, and the vanished-prefix-file case at `:1553-1601`. The runner also reads the muxer list once more after the run, to build the assembly input (`:739`); that read routes through the module's exit direction.

## Goals / Non-Goals

**Goals:**

- One interface answers both directions of "what do the segments on disk and the muxer list mean": what is reusable and where to resume (entry), and how many segments the encoder has closed (exit).
- The three unnamed rules become named, unit-testable behavior: recorded count is the authority for the reusable prefix; a list reaching the end of the timeline completes the task even when fewer segments were cut than the duration implies; a gap in the on-disk prefix truncates it.
- The segment-mark arithmetic (`n x 10 s`) has one definition in the resume path, and the progress offset is read without a data race.

**Non-Goals:**

- **No constant merging.** `kSegmentDurationUs`, `kProbeWindowDurationUs` and `kWindowDurationUs` stay three constants — `reduce-over-engineering` recorded them as distinct domain constants that happen to be equal.
- **No store I/O in the new module.** `jobstate::Store` keeps its persistence interface (12 narrow mutators, 871-line test file); the plan neither reads `Task::segmentIndex` nor calls `markSegmentProgress`.
- **No behavior change** apart from the offset-snapshot race fixed in D6: resume points, persisted marks, progress percentages, assembly input and the ffmpeg command lines are identical.
- **No spec text added for the completion rule.** `video-frame-resume` does not currently describe the "fewer segments than the duration implies" branch; documenting it is a spec change, deferred to its own change rather than smuggled into a refactor.
- **Not this change's job:** probe/preview window policy (a separate candidate), and the `SegmentSeries`-vs-`EncodeConfig` split.

## Decisions

**D1 — One module, two directions, no persistence.**
`src/video/segment_plan.{h,cpp}`, namespace `videoseg`, exposes:

```cpp
struct SegmentPlan {
  std::vector<std::string> reusableNames;  // ordered prefix already on disk
  std::uint64_t resumeUs = 0;              // == segmentMarkUs(reusableNames.size())
  std::uint64_t segmentTotal = 0;          // marks the timeline implies
  bool complete = false;                   // list covers the timeline → assemble only
  std::uint64_t baseFrameOffset = 0;       // frames the bar already counts
  auto startNumber() const -> std::uint64_t { return reusableNames.size(); }
};

// Entry direction: duration + recorded progress + directory + probed frames.
auto planSegments(std::uint64_t totalDurationUs, std::uint64_t storedSegments,
                  fs::path const& segmentDir, std::int64_t totalFrames) -> SegmentPlan;

// Exit direction: the muxer list, read as "segments closed so far".
auto closedSegments(fs::path const& listPath, std::uint64_t startNumber) -> std::uint64_t;

// The one segment-mark arithmetic, shared by both directions.
auto segmentMarkUs(std::uint64_t segmentCount) -> std::uint64_t;
```

The module reads (`fs::exists` walk, `parseSegmentList`, `segmentBaseFrameOffset`) and computes; it does not write, spawn, or lock.

*Alternatives:* (a) extract only the stride arithmetic — leaves `encodeComplete` and the prefix rule inside the 165-line function, still unreachable from unit tests, so the expensive part of the move is paid without the payoff; (b) a stateful tracker holding the store, writing on each closed segment — re-owns persistence that `Store` already owns deeply and makes the plan impure, so the rules could only be tested with a store stand-in.

**D2 — The plan holds facts; the runner builds `SegmentSeries`.**
`SegmentSeries` is the value the ffmpeg command line is built from (`encode_config.h:133-145`), so it stays on the encoder side of the seam. `startNumber()` is derived from `reusableNames.size()` rather than stored, so the count and the names cannot drift — the current code keeps `completed` and `completedNames` as two variables for one fact.

**D3 — The store read stays at the call site.**
`video_encode_runner.cpp:605` is the only production consumer of the recorded segment count; a `PriorProgress` accessor would serve one caller and add a type. The parameter is named `storedSegments` so the seam states what it means.

**D4 — `segmentBaseFrameOffset` stays where it is.**
It is already a pure inline beside the parser (`video_progress_parser.h:37`) and has its own test; the plan calls it. Moving it into the plan would cut the parser's seam for no gain.

**D5 — The exit direction shares the arithmetic, not the guard.**
`closedSegments` returns the raw count; the watcher keeps its monotonic guard (`total <= watch.completedSegments`), the `std::atomic` it guards, and the `Store::markSegmentProgress` call. The guard is watch-loop state, not a rule about segments. The runner's post-run read for the assembly input (`:739`) also counts through `closedSegments(listPath, 0)` — the list was cleared before the run — so `parseSegmentList` has no production caller outside the module.

**D6 — Fix the offset read by locking, not by making fields atomic.**
`getEncodingProgress` will snapshot `baseFrameOffset` and `totalFrames` in one `std::scoped_lock{state.mtx}` alongside the `progressFilePath` it already snapshots (`video_encoding_state.cpp:99-103`). Making `baseFrameOffset` a `std::atomic` would leave the pair readable torn apart from `totalFrames`, and the monitor already takes this lock on every parse pass, so the extra snapshot is free.

**D7 — Tests: unit for the rules e2e cannot stage, e2e untouched.**
`tests/video/segment_plan_tests.cpp`, tag `[segment-plan]`, one `TEST_CASE` per named failure mode: a gap inside the prefix (stops there), the list reaches the timeline end with fewer segments than the duration implies (`complete`), a run that died with a segment in flight (not complete), and the frame-offset conversion. The recorded-count-exceeds-disk variant stays e2e (the vanished-prefix-file case at `tests/e2e/encro_e2e_tests.cpp:1553-1601` removes `seg_0.ts`). The e2e resume cases stay: they cover the process boundary (`-ss`, `-segment_start_number`, the concat manifest) and the watcher writing to job state, which the unit tests cannot reach.

## Risks / Trade-offs

- [The completion rule changes meaning when relocated] → Move it verbatim: same expressions, same inputs, same order; the new unit cases pin all branches, and the e2e resume cases stay green as the outer gate.
- [`totalFrames` is a probe result and can be zero] → Keep today's call shape (`0` when the probe fails); `segmentBaseFrameOffset` already handles it, and the offset test covers the zero case.
- [Watcher double-counts after the change] → `closedSegments` returns a recomputed total, exactly like today's `startNumber + entries.size()`; the monotonic guard stays in the watcher (D5).
- [The seam grows to four inputs] → They are the facts the rules are defined on (duration, recorded count, directory, probe). The frame probe moves before the plan call, where the completion check sits today; it shares `loadCachedOrProbeVideoInfo` with the duration probe, so it is a cache hit. The alternative — handing the module the store — trades four scalars for a persistence dependency.
- [Extra lock in the 250 ms monitor pass] → One uncontended snapshot on a path that already takes the same lock twice per pass.

## Migration Plan

No migration: the module is internal, no persisted format or CLI surface changes, and no state file version is touched. Rollback is a revert of the single refactor commit.
