## Why

Immer (`immer::map` / `immer::vector` / `immer::atom`) is used in six containers across three files, but an audit shows only two of them live on a real cross-thread boundary; the other four have no write contention (built once and read-only afterwards, or purely single-threaded) and merely borrow immer's persistent-API style — including `PendingVidList`, which is pushed into and immediately converted back to `std::vector` via a hand-written `toStdVector`. The two real concurrency points (video-info cache, active encode slots) hold tiny, simple data and do not need lock-free persistent snapshots. Meanwhile the lock audit found one dead field inside a mutex-protected struct (`EncodingState::lastProgress` is written twice, never read) and no overweight locks elsewhere. Removing immer removes a heavy third-party dependency, glue code (`SharedSnapshot` wrapper, `toStdVector`, lambda `update` calls), and makes the remaining synchronization read as plain, direct code.

## What Changes

- **Remove the immer dependency**: drop `add_requires("immer")` and the `immer` package references in `xmake.lua` (default target and both test targets).
- **Replace immer containers with std equivalents**:
  - `src/video/video_process.cpp`: `PendingVidList`/`PendingActionIdList` → `std::vector`; `ActionIdMap` → `appctx::path_map<std::string>` (lookup-only, unordered is fine); `EncodeResultsMap` → `std::map<fs::path, bool>` (ordered — see the ordering note below); delete the `toStdVector` helper; `.set()`/`.push_back()` immutable-style calls become plain mutation.
  - `src/core/app_context.h`: `VideoInfoCacheStore` backed by `immer::atom<immer::map<fs::path, json::value>>` → `std::shared_mutex` + `path_map<json::value>`; public surface (`set`/`find`/`size`) unchanged; the unused `load()` accessor is deleted with the wrapper.
  - `src/video/video_batch_execution.h`: active-slot snapshot backed by `immer::atom<immer::vector<EncodingStatePtr>>` → `std::mutex` + `std::vector<EncodingStatePtr>`; monitor read path (`activeStates`, `activeState`) and worker write path (`setActive`, `clearActive`) keep their signatures.
- **Delete the dead `EncodingState::lastProgress` field**: written in `renderProgress`/`finalizeState` but never read; readers use `lastProgressAtomic` (kept).
- **Tests**: existing cache tests (`app_context_tests.cpp`, `video_info_tests.cpp`) exercise only the `set`/`find` interface and stay unchanged; two test names mentioning "immer snapshot" are reworded. Add tests for the stock `ActionIdMap`/slots paths only where the old tests left a gap (none expected — the types are internal).
- **Only user-visible change is iteration order**: `immer::map` is a HAMT (unordered iteration), so today's failure list (`printEncodingSummary`) and zip member order (`collectEncodedOutputFiles`) follow hash order; `std::map` makes them deterministic path-sorted order instead. e2e asserts only the "Failed to encode: N" count line, never the ordering, so no test goes red. Everything else — CLI surface, file formats, job-state, progress, encoding semantics — is unchanged.

## Capabilities

This is an internal refactor: no spec-level requirements change (the only observable difference is deterministic path-sorted iteration order of failure lists and zip members — unspecified before, since immer::map is a HAMT). `skip_specs: true` is set in `.openspec.yaml`; no spec deltas are created.

## Impact

- `xmake.lua` (3 targets), `plugins/coverage/xmake.lua` (immer in the ignore-regex list for third-party headers if still applicable; likely removed with the dependency)
- `src/core/app_context.h` (cache store, `EncodingState`)
- `src/video/video_process.cpp` (pending lists, action-id/results maps, `toStdVector`)
- `src/video/video_batch_execution.h` (snapshot/slots)
- `src/video/video_batch_execution.cpp`, `src/video/video_encoding_state.cpp` (call sites of the changed APIs only; no logic change)
- Type aliases `videobatch::ActionIdMap` / `videobatch::EncodeResultsMap` are kept (re-pointed at the std containers) so test files that construct them (`tests/video_batch_execution_tests.cpp`, `tests/encode_probe_tests.cpp`) compile unchanged.
- Tests: `tests/app_context_tests.cpp`, `tests/video_info_tests.cpp` (name-only reword), plus the existing unit/e2e suites as regression gate.
- Dependency removal: immer no longer needed; no new dependencies. Style preserved (East const, trailing returns, C++26, clang-format).
