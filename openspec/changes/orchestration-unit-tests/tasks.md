# Tasks: orchestration-unit-tests

## 1. Batch orchestration branches (video_batch_execution_tests.cpp)

- [ ] 1.1 Add test: confirmation declined (`yesToAll=false`, stdin EOF) returns a `nullopt`-results outcome and the invocation log shows no encode invocations (probe-stage scoring calls are expected before the prompt).
- [ ] 1.2 Add test: verbose path (`config.verbose=true`) runs `runEncodingWithoutProgress` sequentially; invocation log shows per-file encodes in order; job-state records transition running → succeeded.
- [ ] 1.3 Add test: verbose path with a failing file records failed job-state with the failure reason and continues with remaining files.
- [ ] 1.4 Add test: stop request raised before the probe stage yields the Aborted outcome (no encode calls); stop request raised mid-verbose-loop breaks the loop (remaining files unattempted).
- [ ] 1.5 Add test: skip-encode plans (estimated output > source) are excluded from encode calls (invocation log has no entry for the skipped file) and the returned outcome's results exclude the skipped path - these proxies stand in for progress-state totals, which are internal to `runEncodingTasks` (the skippedBeforeStart wiring into the overall bar's completed base is asserted indirectly through them).
- [ ] 1.6 Run `xmake test-report --tag="[video-batch-execution]"`; confirm green.

## 2. Webp fallback chain (video_encode_runner_tests.cpp, new file)

- [ ] 2.1 Confirm `ENCRO_FAKE_FFMPEG_OUTPUT_BYTES` shapes the webp step's output size for the tests below (audit: it writes a real sparse file of the requested size, so no new knob is expected).
- [ ] 2.2 Add test: webp target size met on first attempt - invocation log shows exactly one encode call, no fallback quality attempt.
- [ ] 2.3 Add test: target missed → `webpMinQualityFallback` engages after the quality ladder exhausts - with uniformly oversized output the invocation log shows 7 attempts (q=80..20), the final attempt carries q=20, and success is claimed via the fallback.
- [ ] 2.4 Add test: stop request during the webp retry window aborts (`abortWebpForStopRequest`) - no success claim in state, no further encode calls.
- [ ] 2.5 Add test: `clearWebpStaleFiles` removes stale progress/output leftovers before the run (create stale files, pre-set `state.progressFilePath` so both paths are controlled - `prepareEncodeExecution` only generates a uuid path when it is unset - run, assert gone).
- [ ] 2.6 Run `xmake test-report`; confirm green.

## 3. Segmented encoding (video_encode_runner_tests.cpp)

- [ ] 3.1 Add test: single-segment happy path - invocation log shows segment encode + concat (or direct assembly), audio extracted once (write a custom probe JSON containing an audio stream to `ENCRO_FAKE_FFPROBE_JSON_FILE`; the default JSON is video-only).
- [ ] 3.2 Add test: mid-segment failure (FAIL_MATCH on a segment name) - failed state recorded, no assemble/concat invocation.
- [ ] 3.3 Add test: assemble/concat failure - error surfaces to the encoding state with the assemble step identified.
- [ ] 3.4 Add test: multi-segment run extracts audio exactly once across segment encodes (needs `ENCRO_FAKE_FFPROBE_DURATION_SECS` > 10; the default 2.0 yields exactly one segment; same custom audio probe JSON as 3.1).
- [ ] 3.5 Run `xmake test-report`; confirm green.

## 4. ffprobe JSON parse corners (video_info_tests.cpp)

- [ ] 4.1 Seed `RuntimeContext::videoInfoCache` and drive public `getVidTotalDurationUs`; add table-driven SECTIONs for fraction parsing ("N/A", "0/0", empty, negative, oversized numerator).
- [ ] 4.2 Via the same cache-seeded route, drive `getVidTotalFrames`/dimension extraction: missing keys, empty stream arrays, guarded non-numeric corners ("N/A", empty). The unguarded `stoll` throw for arbitrary non-numeric nb_frames is a separate production finding - out of scope here, do not canonicalize it in a test.
- [ ] 4.3 Drive the input-collection predicates through public `readAllVids` (webp config, `ctx.toolchain.ffprobePath` pointed at a fake copy) and `readAllVidsFromFiles`: boundary sizes at the webp size limit, known/unknown extensions.
- [ ] 4.4 Run `xmake test-report`; confirm green.

## 5. Final verification

- [ ] 5.1 Run `xmake test-parallel`; confirm all shards green.
- [ ] 5.2 Run `xmake coverage --e2e --summary`; record after-numbers for `video_batch_execution.cpp`, `video_encode_runner.cpp`, `video_info.cpp` against the proposal's expected ranges.
