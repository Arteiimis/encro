# Tasks: orchestration-unit-tests

## 1. Batch orchestration branches (video_batch_execution_tests.cpp)

- [x] 1.1 Add test: confirmation declined (`yesToAll=false`, stdin EOF) returns a `nullopt`-results outcome and the invocation log shows no encode invocations (probe-stage scoring calls are expected before the prompt).
- [x] 1.2 Add test: verbose path (`config.verbose=true`) runs `runEncodingWithoutProgress` sequentially; invocation log shows per-file encodes in order; job-state records transition running → succeeded.
- [x] 1.3 Add test: verbose path with a failing file records failed job-state with the failure reason and continues with remaining files.
- [x] 1.4 Add test: stop request raised before the probe stage yields the Aborted outcome (no encode calls); stop request raised mid-verbose-loop breaks the loop (remaining files unattempted).
- [x] 1.5 Add test: skip-encode plans (estimated output > source) are excluded from encode calls (invocation log has no entry for the skipped file) and the returned outcome's results exclude the skipped path - these proxies stand in for progress-state totals, which are internal to `runEncodingTasks` (the skippedBeforeStart wiring into the overall bar's completed base is asserted indirectly through them).
- [x] 1.6 Run `xmake test-report --tag="[video-batch-execution]"`; confirm green.

## 2. Webp fallback chain (video_encode_runner_tests.cpp, new file)

- [x] 2.1 Confirm `ENCRO_FAKE_FFMPEG_OUTPUT_BYTES` shapes the webp step's output size for the tests below (audit: it writes a real sparse file of the requested size, so no new knob is expected).
- [x] 2.2 Add test: webp target size met on first attempt - invocation log shows exactly one encode call, no fallback quality attempt.
- [x] 2.3 Add test: target missed → `webpMinQualityFallback` engages after the quality ladder exhausts - with uniformly oversized output the invocation log shows 7 attempts (q=80..20), the final attempt carries q=20, and success is claimed via the fallback. (Fixture: `OUTPUT_BYTES=25165824` (24 MiB, 4 MiB over the 20 MiB target) so the size gap exceeds the 3 MiB small-gap threshold and the coarse q-step-10 ladder runs 80,70,...,20. A 21 MiB output would have been only 1 MiB over and triggered the fine q-step-5 ladder with 13 attempts.)
- [x] 2.4 Add test: stop request during the webp retry window aborts (`abortWebpForStopRequest`) - no success claim in state, no further encode calls.
- [x] 2.5 Add test: `clearWebpStaleFiles` removes stale progress/output leftovers before the run (create stale files, pre-set `state.progressFilePath` so both paths are controlled - `prepareEncodeExecution` only generates a uuid path when it is unset - run, assert gone).
- [x] 2.6 Run `xmake test-report`; confirm green.

## 3. Segmented encoding (video_encode_runner_tests.cpp)

- [x] 3.1 Add test: single-segment happy path - invocation log shows segment encode + concat (or direct assembly), audio extracted once (write a custom probe JSON containing an audio stream to `ENCRO_FAKE_FFPROBE_JSON_FILE`; the default JSON is video-only).
- [x] 3.2 Add test: mid-segment failure (FAIL_MATCH on a segment name) - failed state recorded, no assemble/concat invocation.
- [x] 3.3 Add test: assemble/concat failure - error surfaces to the encoding state with the assemble step identified. (Production `assembleSegments` only `LOG_WARN`s — it never sets `state.lastError` — so the test identifies the step via the invocation log: segments ran and a `-f concat` invocation targeted the planned output, while `state.success` is false.)
- [x] 3.4 Add test: multi-segment run extracts audio exactly once across segment encodes (needs `ENCRO_FAKE_FFPROBE_DURATION_SECS` > 10; the default 2.0 yields exactly one segment; same custom audio probe JSON as 3.1).
- [x] 3.5 Run `xmake test-report`; confirm green.

## 4. ffprobe JSON parse corners (video_info_tests.cpp)

- [x] 4.1 Seed `RuntimeContext::videoInfoCache` and drive public `getVidTotalDurationUs`; add table-driven SECTIONs for fraction parsing ("N/A", "0/0", empty, negative, oversized numerator). (Zero-denominator / negative / oversized fraction corners are exercised on the `getVidTotalFrames` rate-fallback path, which is where `parseFraction` runs; the duration path only parses a plain double, so it covers N/A/empty/non-object.)
- [x] 4.2 Via the same cache-seeded route, drive `getVidTotalFrames`/dimension extraction: missing keys, empty stream arrays, guarded non-numeric corners ("N/A", empty). The unguarded `stoll` throw for arbitrary non-numeric nb_frames is a separate production finding - out of scope here, do not canonicalize it in a test.
- [x] 4.3 Drive the input-collection predicates through public `readAllVids` (webp config, `ctx.toolchain.ffprobePath` pointed at a fake copy) and `readAllVidsFromFiles`: boundary sizes at the webp size limit, known/unknown extensions.
- [x] 4.4 Run `xmake test-report`; confirm green.

## 5. Final verification

- [x] 5.1 Run `xmake test-parallel`; confirm all shards green.
- [x] 5.2 Run `xmake coverage --e2e --summary`; record after-numbers for `video_batch_execution.cpp`, `video_encode_runner.cpp`, `video_info.cpp` against the proposal's expected ranges.
  - video_batch_execution.cpp line 89.22% (proposal ~70%+) ✓
  - video_encode_runner.cpp line 71.90% (proposal ~85%+; the remainder is the untested monitor-thread rendering and the non-webp quality ensembles that a fake tool cannot reach)
  - video_info.cpp line 75.83% (proposal ~75%+) ✓
