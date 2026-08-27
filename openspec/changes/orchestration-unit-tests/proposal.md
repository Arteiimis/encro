# Proposal: orchestration-unit-tests

## Why

After the coverage-scope fix lands (coverage-include-e2e), the remaining low-coverage layers are the real gaps: the video orchestration code paths that decide fallback, cancellation, and assembly. These are the branches where a regression silently produces a bad output file or a stuck batch - webp adaptive quality fall-back (`encodeWebpWithTargetSize` → `webpMinQualityFallback` → `abortWebpForStopRequest`), segmented encoding (`runSegmentedEncoding` / `assembleSegments` / `ensureAudioFile`), the batch-level decisions in `runEncodingTasks` (confirmation declined, verbose no-progress path, mid-probe stop request, skip-encode accounting), and the pure ffprobe-JSON parsing branches in `video_info.cpp`. Today these run mostly under e2e (happy paths) or not at all (fallback sequences, decode-side parse corners), so unit-level failure isolation does not exist.

## What Changes

- Add unit tests for the `runEncodingTasks` orchestration branches in `tests/video/video_batch_execution_tests.cpp` using the established fake-tool in-process pattern (`video_process_orchestration_tests.cpp` as the template): confirmation declined (returns `nullopt` outcome, no encode invoked), verbose path (`runEncodingWithoutProgress` sequential execution, per-file job-state transitions, stop-request break), mid-probe stop request (Aborted outcome), and skip-encode accounting (skipped files count toward the overall bar's completed base).
- Add unit tests for the webp fallback chain in `tests/video/video_encode_runner_tests.cpp` (new file): target-size met on first attempt; target missed → min-quality fallback engages and succeeds; stop request during webp retry aborts without partial output claims; stale WebP progress/output cleanup runs (`clearWebpStaleFiles`).
- Add unit tests for segmented encoding: single segment happy path; mid-segment failure marks failed state and does not assemble; concat/assemble failure surfaces the assemble error; audio extracted once across segments (audio-only-once is currently e2e-only).
- Add parsing-branch tests to `tests/video/video_info_tests.cpp`: fraction/duration/dimension parse corners ("N/A", zero-denominator fractions, non-numeric nb_frames, missing stream keys) - pure functions, cheap table-driven cases.
- Fake-tool extensions keep to the existing environment-variable mechanism (e.g. an output-size knob for triggering the target-size fallback path); no new infrastructure beyond what `portable-fake-tool` already prescribes.
- Monitor-thread rendering (`video_encoding_state.cpp`) is explicitly out of scope: jthread + terminal-render timing makes unit cost exceed value; it stays covered indirectly by e2e.

## Capabilities

### New Capabilities

### Modified Capabilities

(No capability deltas: production behavior is unchanged and the fake tool keeps its env-var mechanism, which the `portable-fake-tool` spec already mandates for behavior extensions. Declared `skip_specs: true`.)

## Impact

- `tests/video/video_batch_execution_tests.cpp`, `tests/video/video_encode_runner_tests.cpp` (new), `tests/video/video_info_tests.cpp`: ~+40-50 unit cases total (estimate; driven by the branch list above).
- `tests/e2e/fake_media_tool.cpp`: at most 1-2 new env knobs (output-size control for webp target fallback), following the existing knob pattern.
- `src/`: untouched; tests target existing exports. Any seam needed (e.g. reaching `runEncodingWithoutProgress` via `runEncodingTasks` with `--verbose` config) uses public entry points only - no production visibility changes.
- Coverage effect: `video_batch_execution.cpp` 35.8% → ~70%+, `video_encode_runner.cpp` 67.3% → ~85%+, `video_info.cpp` 59.6% → ~75%+ (post-e2e-merge baseline).
- Depends on: `test-suite-cleanup` (avoids building on files it empties/renames); independent of `coverage-include-e2e` but sequenced after for a clean before/after number.
