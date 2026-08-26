## 1. Build files

- [ ] 1.1 Remove `add_requires("immer")` and the `immer` entry from all three `add_packages(...)` lists in `xmake.lua`
- [ ] 1.2 Remove `immer` from the third-party header ignore regex in `plugins/coverage/xmake.lua`

## 2. Video info cache store (app_context.h)

- [ ] 2.1 Replace `immer::atom<immer::map<fs::path, json::value>>` with `std::shared_mutex` + `path_map<json::value>` inside `VideoInfoCacheStore`; keep `set`/`find`/`size` signatures and copy semantics; drop the immer includes
- [ ] 2.2 Delete the dead `EncodingState::lastProgress` field (keep `lastProgressAtomic`); remove its writes in `video_encoding_state.cpp` `renderProgress` and `video_batch_execution.h` `finalizeState`
- [ ] 2.3 Update `tests/app_context_tests.cpp` and both cache tests in `tests/video_info_tests.cpp` to drop the "immer snapshot" naming (assertions unchanged)

## 3. Active encode slots (video_batch_execution.h)

- [ ] 3.1 Replace `SharedSnapshot`/`ActiveSlots` (`immer::atom<immer::vector<EncodingStatePtr>>`) with `std::mutex` + `std::vector<EncodingStatePtr>` sized once at construction; drop `makeInitialSnapshot` and immer includes
- [ ] 3.2 Rewrite `setActive`/`clearActive`/`activeState`/`activeStates`/`loadShared` over the mutex-protected vector (signatures unchanged); update call sites in `video_batch_execution.cpp` if any glue disappears

## 4. Single-threaded containers (video_process.cpp)

- [ ] 4.1 Replace `PendingVidList`/`PendingActionIdList` with `std::vector`; delete the `toStdVector` helper and its call sites (`pendingVids`, `pendingActionIds`)
- [ ] 4.2 Replace `ActionIdMap` with `path_map<std::string>` (lookup-only) and update `prepareEncodeActions` to plain `emplace`
- [ ] 4.3 Replace `EncodeResultsMap` with `std::map<fs::path, bool>` (ordered — keeps summary/zip ordering stable); update `mergeEncodeResults`, `runEncodingWithoutProgress` (`video_batch_execution.cpp`), and `collectEncodingResults` (`video_batch_execution.cpp`) to plain mutation/emplace

## 5. Verification

- [ ] 5.1 `xmake build encro` clean; `rg -i immer src tests xmake.lua plugins` returns only no-immmer-hit (no references left)
- [ ] 5.2 `xmake test-report` green (unit suite incl. app-context/video-info tests)
- [ ] 5.3 `xmake build e2e_tests && xmake run e2e_tests` green (fake-tool e2e)
- [ ] 5.4 `xmake test-parallel` green (real-ffmpeg shards; exercise parallel encode + monitor paths)
- [ ] 5.5 `xmake fmt -k` and `xmake tidy` (report-only) show no new findings in touched files
