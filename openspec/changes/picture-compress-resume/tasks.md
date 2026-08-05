## 1. Cleanup

- [ ] 1.1 Delete `packAllPicsToZip` (declaration picture_process.h:15 + definition picture_process.cpp:529-616); grep for references, then verify by compiling
- [ ] 1.2 Fold the two verbatim copies of `canceledExitCodeForPromptAbort` into a `stopsignal` inline function; update both call sites (video_process.cpp:281, picture_process.cpp:185)
- [ ] 1.3 picture_compress.cpp: extract a shared helper for the common "compress success → size comparison → delete oversized output → record result" segment in compressImageTask (~75-99) and retryFailedTasks (~221-243)
- [ ] 1.4 `applyEntryNameOverrides` (pack.cpp:267): skip entries with `isSummary`; pack_execute_test new case: SummaryConfig + entryNameForFile set together must not overwrite summary entry names

## 2. Always-on state + matched flag

- [ ] 2.1 `shouldEnableJobState` (pipeline.cpp:35): add branch `processType == "picture" && compressImages` → true
- [ ] 2.2 `RuntimeContext`: add `bool jobStateMatched`; `ensureJobState` sets it to `initRes.value()`
- [ ] 2.3 pipeline_picture_tests: new cases — compressImages creates state without flags; direct pack does not (existing test 61-77 is the negative assertion)

## 3. Compression-phase marker task

- [ ] 3.1 jobstate: support a phase marker task — stable id (e.g. `compress-phase`), own kind, reusing mergeTasks/markRunning/markInterrupted/markSucceeded
- [ ] 3.2 picture_process: merge the marker task when the compression batch starts and markRunning; markInterrupted on cancel; markSucceeded when the phase finishes successfully
- [ ] 3.3 Tests: cancel mid-compression → state kept (task table non-empty, jobStateNeverStarted=false); cancel at confirmation prompt → state removed (existing pipeline_picture_tests 79-103 must not regress)

## 4. Atomic compression outputs

- [ ] 4.1 `compressImage`: write `{finalPath}.partial`; after ffmpeg exit 0 and output exists, rename to the final name (leftover .partial is overwritten by `-y`)
- [ ] 4.2 Tests: non-zero exit / killed producer → final file absent, only .partial remains; success → final file exists and .partial is gone

## 5. Cache directory and lifecycle

- [ ] 5.1 Cache dir becomes quality-keyed `.compress_tmp_q{quality}` (default 2 materialized); update addCompressTask and temp-path derivation
- [ ] 5.2 Startup cleanup rule: remove all `.compress_tmp*` dirs in the output dir except the current key; when `jobStateMatched && current key dir exists`, keep the current key dir, otherwise delete and rebuild
- [ ] 5.3 Conditional lifecycle: cancel → keep; success and fatal errors → remove; reorder the `remove_all(tempDir)` at picture_process.cpp:488 after the cancel check and make it conditional per the table in design.md D7
- [ ] 5.4 Tests: successful run clears the cache; canceled run keeps it; quality change does not reuse the other key dir; `--restart` clears all caches; missing state file invalidates the cache → full recompression

## 6. Resume skip

- [ ] 6.1 When building CompressTasks, skip pictures satisfying "final output exists && output mtime >= source mtime" (applies to summary and regular entries)
- [ ] 6.2 Tests: cancel after partial compression then rerun → only remaining files compress, completed ones are packed from cache; replaced source (same name, new content) → recompressed and the archive contains the fresh output

## 7. Pack-planning decision moved up

- [ ] 7.1 resolveSource becomes a filesystem three-state pure function (output exists && <= source → use output + .jpg name; output larger → use original; no output or missing source → skip); delete buildCompressedResultLookup/buildCompressTaskKey
- [ ] 7.2 `CompressResult`: remove `usedCompressed`, clean up all use sites (picture_process.cpp:453, picture_compress.cpp:80-98, 223-242); compressImageBatch's return value only serves counting and the "all failed" check
- [ ] 7.3 Tests: deterministic three states (smaller/larger/missing); all compressions fail → error and nothing packed

## 8. Integration verification

- [ ] 8.1 `xmake build encro && xmake build tests && xmake run tests` all green
- [ ] 8.2 `xmake build e2e_tests && xmake run e2e_tests` all green (fake ffmpeg covers interruption/exit-code paths)
- [ ] 8.3 `xmake format -k check` passes
