# Video Encode Performance Optimization Plan

This plan is the working proposal for improving video encoding runtime performance by reducing unnecessary CPU work, lock contention, and wasteful I/O in the hot paths of the encoding pipeline — without changing encoding quality or output behavior.

## Goals

- Reduce per-task overhead in the progress monitoring loop (the hottest shared path during encoding).
- Eliminate redundant ffprobe invocations and JSON re-parsing in the 20ms monitor cycle.
- Lower lock contention between the monitor thread and worker threads on shared `EncodingState` objects.
- Shorten WebP adaptive encoding by adding early size-based heuristics.
- Reduce monitoring CPU usage by adjusting poll frequency to match real UI needs.
- Clean up minor repeated allocations and redundant work visible in the hot path.
- Keep all encoding output, progress UI appearance, and job-state recovery semantics unchanged.

## Non-Goals

- Do not change the concurrency model or replace `std::jthread` / `immer::atom` usage.
- Do not change ffmpeg encoding parameters, codec selection, CRF, or WebP quality algorithm.
- Do not alter progress bar rendering, terminal output format, or progress file parsing.
- Do not redesign the output planning, naming, or packaging workflow.
- Do not rewrite job-state persistence or recovery logic.
- Do not change the public API of `encodeToHevc`, `handlePathEncoding`, or `handleMultiFileEncoding`.

## Current Assessment

The video process module has been structurally refactored (see `VIDEO_PROCESS_REFACTOR_CHECKLIST.md`) with clear module separation. The primary remaining performance headroom is in the **runtime encoding execution path**, specifically:

### Hotspot 1: Progress Monitor Loop (`video_batch_execution.cpp`)

The `startEncodingMonitor` thread runs at 20ms (50 Hz) and on each tick:

- Calls `getVidTotalFrames()` per active task, which loads the `immer::map`-cached `json::value` and re-parses the JSON object to extract total frame count — even though this value never changes for a given file.
- Calls `activeStates()` which loads the immer `atom<SharedSnapshot>` to collect all active states.
- In `updateOverall()`: iterates all active states, acquires a `std::scoped_lock` on each to read `lastProgress`, releasing it immediately.

This is the single hottest code path during encoding.

### Hotspot 2: `updateOverall()` Lock Overhead

`updateOverall()` runs on every monitor tick. Accessing `lastProgress` requires per-state mutex locking even though the field could be an atomic. For N active tasks at 50 Hz, this generates N×50 lock acquisitions per second.

### Hotspot 3: WebP Adaptive Encoding Starting Quality

`encodeWebpWithTargetSize()` always starts at `quality = 80`. For large input files this can waste 1–3 full ffmpeg passes before reaching a size under 20 MB. There is no input-size heuristic to choose a smarter starting quality.

### Hotspot 4: `finalizeState()` Triple Locking

`finalizeState()` locks the same `EncodingState` mutex in three sequential critical sections instead of one.

### Hotspot 5: `progressFilePath` Ownership Ambiguity

`progressFilePath` can be generated in both `createEncodingState()` and `prepareEncodeExecution()`. While the `has_value()` guard prevents double generation, the dual ownership makes the execution flow harder to reason about.

### Hotspot 6: `immer::map` Usage in Sequential Path

`runEncodingWithoutProgress()` builds the results map by calling `vidsRunRes.set(...)` per task, creating a new persistent tree node on each insertion. This is negligible for small batches but wasteful for large ones.

### Hotspot 7: `finalizeVideoList()` ffprobe Before Filtering

For mp4 output, `finalizeVideoList()` launches parallel ffprobe on all scanned candidates before the workflow has a chance to skip already-completed tasks (via job-state recovery). Some ffprobe work may never be used.

## Execution Order

### Phase 0: Guard Rails

- [x] Establish baseline timing for the monitor loop and `getVidTotalFrames` on a realistic batch.
- [x] Record the narrowest commands that build and run all video-process and orchestration tests.
- [x] Verify baseline test pass before any perf changes.

Verified on Windows with:

- `xmake build tests` — builds in ~18s (incremental)
- `.\build\windows\x64\release\tests.exe "[video-process]"` — 113 assertions, 32 cases, all passed
- `.\build\windows\x64\release\tests.exe "[video-process][orchestration]"` — 22 assertions, 4 cases, all passed
- `.\build\windows\x64\release\tests.exe "[video-process][parseProgressFile]"` — 2 assertions, 1 case, all passed
- `.\build\windows\x64\release\tests.exe "[video-process][pack]"` — 22 assertions, 7 cases, all passed
- `.\build\windows\x64\release\tests.exe` (full suite) — **702 assertions, 164 cases, all passed**
- `xmake build e2e_tests` — pre-existing build error (missing `infra/stop_signal.h` in e2e `test_utils.h` include path, unrelated to this plan)

**Baseline recorded: 2026-04-24**

Exit criteria:

- [x] All existing video-process tests pass before optimization begins.
- [x] Baseline timing is recorded (manual observation or instrumentation).

### Phase 1: Cache `totalFrames` in `EncodingState` ✅

**Rationale**: `getEncodingProgress()` calls `getVidTotalFrames()` every monitor tick. The result is invariant per file. Caching as `int64_t` avoids the JSON re-parse and immer-map lookup on every poll.

**Changes applied**:

- [x] `app_context.h`: Added `std::optional<int64_t> totalFrames;` to `EncodingState`
- [x] `video_batch_execution.cpp`: `getEncodingProgress()` checks `state.totalFrames` first; computes via `getVidTotalFrames` and caches on first call only

**Verification**:

- `xmake build tests` — passed
- `.\build\windows\x64\release\tests.exe "[video-process]"` — 113 assertions, 32 cases, all passed
- Full suite: **702 assertions, 164 cases, all passed** (unchanged from baseline)

Exit criteria:

- [x] `getVidTotalFrames()` is called at most **once** per file in the monitor path.
- [x] JSON frame-count parsing is eliminated from the 20ms hot path.
- [x] All existing tests still pass.

### Phase 2: Convert `lastProgress` to Atomic ✅

**Rationale**: `updateOverall()` locks each active `EncodingState` mutex just to read `lastProgress`. Using `std::atomic<float>` eliminates the lock on the read side.

**Changes applied**:

- [x] `app_context.h`: Added `#include <atomic>` and `std::atomic<float> lastProgressAtomic{-1.0f};` to `EncodingState`
- [x] `video_batch_execution.cpp`:
  - `updateOverall()`: reads `activeState->lastProgressAtomic.load(std::memory_order_acquire)` without acquiring the mutex
  - Monitor loop: writes `lastProgressAtomic.store(p, std::memory_order_release)` alongside `lastProgress = p`
  - `finalizeState()`: writes `lastProgressAtomic.store(100.0f, std::memory_order_release)` alongside `lastProgress = 100.0f`

**Verification**:

- `xmake build tests` — passed
- `.\build\windows\x64\release\tests.exe "[video-process]"` — 113 assertions, 32 cases, all passed
- Full suite: **702 assertions, 164 cases, all passed** (unchanged from baseline)

Exit criteria:

- [x] `updateOverall()` no longer acquires any `EncodingState` mutex.
- [x] Progress reads in the monitor thread are lock-free.
- [x] All existing tests still pass.

### Phase 3: Adjust Monitor Poll Interval

**Rationale**: 20ms (50 Hz) polling is excessive for terminal progress rendering. 100ms (10 Hz) provides visually smooth updates with 5× fewer cycles.

**Changes to `video_batch_execution.cpp`** — `startEncodingMonitor()`:

- Change `std::this_thread::sleep_for(20ms)` → `std::this_thread::sleep_for(100ms)`.
- Optionally implement adaptive: 50ms for first 5 seconds, 200ms thereafter.

**Verification**:

- `xmake build tests`
- `xmake run tests "[video-process][orchestration]"`
- Manual validation: run a multi-file encoding, observe progress bar smoothness.

Exit criteria:

- Monitor thread CPU usage is observably lower (task manager or similar).
- Progress updates appear smooth to the user.

### Phase 4: Merge `finalizeState()` Critical Sections

**Rationale**: Three sequential `scoped_lock{vidState->mtx}` blocks on the same object.

**Changes to `video_batch_execution.cpp`** — `EncodingExecutionContext::finalizeState()`:

- Merge the first two locked blocks into one.
- Keep `fs::remove(progressFilePath)` outside the lock (already has its own error_code guard, I/O should not block other readers).

**Verification**:

- `xmake build tests`
- `xmake run tests "[video-process]"`

Exit criteria:

- `finalizeState()` locks `vidState->mtx` at most **twice** (once for state fields, once for I/O condition check), ideally **once**.

### Phase 5: WebP Starting Quality Heuristic

**Rationale**: Starting WebP quality at 80 wastes passes for large inputs. A simple input-size heuristic can choose better initial quality.

**Changes to `video_encode_runner.cpp`** — `encodeWebpWithTargetSize()`:

- Before the quality loop, read `fs::file_size(encodeCtx.inputVidPath)`.
- If input > 100 MB, start at `quality = 50`.
- If input > 200 MB, start at `quality = 40`.
- Otherwise keep `quality = 80`.

**Verification**:

- `xmake build tests`
- `xmake run tests "[video-process]"`
- Manual: encode a large (200+ MB) file to WebP, observe fewer retry passes.

Exit criteria:

- WebP encoding of large files uses fewer ffmpeg passes while still finding target size.
- Small files retain the high-quality default.

### Phase 6: Single-Owner `progressFilePath`

**Rationale**: `createEncodingState()` already generates the progress file path. `prepareEncodeExecution()` has redundant fallback code that never executes in the batched path.

**Changes to `video_encode_runner.cpp`** — `prepareEncodeExecution()`:

- Remove the `if (!state.progressFilePath.has_value())` fallback.
- Expect the caller to always set it before calling `encodeToHevc`.

**Verification**:

- `xmake build tests`
- `xmake run tests "[video-process]"`
- `xmake run e2e_tests`

Exit criteria:

- `progressFilePath` is set in exactly one place (the caller), never redundantly re-generated.

### Phase 7: Sequential Path `immer::map` → `std::unordered_map`

**Rationale**: `runEncodingWithoutProgress()` uses `vidsRunRes.set(vidPath, success)` in a tight sequential loop, causing persistent tree allocations.

**Changes to `video_batch_execution.cpp`** — `runEncodingWithoutProgress()`:

- Replace `videobatch::EncodeResultsMap` with local `std::unordered_map<fs::path, bool>` during the loop.
- Convert to `immer::map` at the end for return type compatibility.

**Verification**:

- `xmake build tests`
- `xmake run tests "[video-process]"`

Exit criteria:

- Sequential encoding path allocates fewer intermediate tree nodes.
- Behavior and return type unchanged.

## Validation Commands

Use the narrowest command possible after each phase.

- Build tests: `xmake build tests`
- Run video tests: `xmake run tests "[video-process]"`
- Run orchestration tests: `xmake run tests "[video-process][orchestration]"`
- Run e2e tests: `xmake run e2e_tests`
- Run all unit tests: `xmake run tests`

## Done Definition

- Monitor thread no longer calls `getVidTotalFrames()` on every tick — total frame count is cached per file after first resolve.
- `updateOverall()` reads progress without acquiring `EncodingState` mutexes.
- Monitor poll interval is raised to at least 100ms.
- `finalizeState()` locks the state mutex at most once for field writes.
- WebP adaptive encoding starts at a quality appropriate for input file size.
- `progressFilePath` has a single point of ownership.
- All existing tests still pass.
- Encoding output, progress UI appearance, and job-state recovery are unchanged.
- Each phase was validated independently before moving to the next.
