# Tasks: orchestration-unit-tests

## 1. Batch orchestration branches (video_batch_execution_tests.cpp)

- [ ] 1.1 Add test: confirmation declined (`yesToAll=false`, stdin EOF) returns a `nullopt`-results outcome and the invocation log shows zero encode calls.
- [ ] 1.2 Add test: verbose path (`config.verbose=true`) runs `runEncodingWithoutProgress` sequentially; invocation log shows per-file encodes in order; job-state records transition running → succeeded.
- [ ] 1.3 Add test: verbose path with a failing file records failed job-state with the failure reason and continues with remaining files.
- [ ] 1.4 Add test: stop request raised before the probe stage yields the Aborted outcome (no encode calls); stop request raised mid-verbose-loop breaks the loop (remaining files unattempted).
- [ ] 1.5 Add test: skip-encode plans (estimated output > source) are excluded from encode calls and counted into the overall bar's completed base (assert via progress state totals).
- [ ] 1.6 Run `xmake test-report --tag="[video-batch-execution]"`; confirm green.

## 2. Webp fallback chain (video_encode_runner_tests.cpp, new file)

- [ ] 2.1 Verify whether `ENCRO_FAKE_FFMPEG_OUTPUT_BYTES` shapes the webp step's output size; if not, add one env knob to `fake_media_tool.cpp` following the existing knob pattern.
- [ ] 2.2 Add test: webp target size met on first attempt - invocation log shows exactly one encode call, no fallback quality attempt.
- [ ] 2.3 Add test: target missed → `webpMinQualityFallback` engages - invocation log shows the second attempt at min quality and success.
- [ ] 2.4 Add test: stop request during the webp retry window aborts (`abortWebpForStopRequest`) - no success claim in state, no further encode calls.
- [ ] 2.5 Add test: `clearWebpStaleFiles` removes stale progress/output leftovers before the run (create stale files, run, assert gone).
- [ ] 2.6 Run `xmake test-report`; confirm green.

## 3. Segmented encoding (video_encode_runner_tests.cpp)

- [ ] 3.1 Add test: single-segment happy path - invocation log shows segment encode + concat (or direct assembly), audio extracted once.
- [ ] 3.2 Add test: mid-segment failure (FAIL_MATCH on a segment name) - failed state recorded, no assemble/concat invocation.
- [ ] 3.3 Add test: assemble/concat failure - error surfaces to the encoding state with the assemble step identified.
- [ ] 3.4 Add test: multi-segment run extracts audio exactly once across segment encodes (assert audio-extraction call count in the log).
- [ ] 3.5 Run `xmake test-report`; confirm green.

## 4. ffprobe JSON parse corners (video_info_tests.cpp)

- [ ] 4.1 Add table-driven SECTIONs for fraction parsing ("N/A", "0/0", empty, negative, oversized numerator).
- [ ] 4.2 Add SECTIONs for duration/dimension/nb_frames extraction: missing keys, non-numeric values, empty stream arrays.
- [ ] 4.3 Add SECTIONs for input-collection predicates (`keepsWebpInputSizeLimit`, `keepScannedVideoCandidate`): boundary sizes, known/unknown extensions.
- [ ] 4.4 Run `xmake test-report`; confirm green.

## 5. Final verification

- [ ] 5.1 Run `xmake test-parallel`; confirm all shards green.
- [ ] 5.2 Run `xmake coverage --e2e --summary`; record after-numbers for `video_batch_execution.cpp`, `video_encode_runner.cpp`, `video_info.cpp` against the proposal's expected ranges.
