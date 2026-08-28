# Design: orchestration-unit-tests

## Context

The fake-tool in-process pattern is established (`tests/video/video_process_orchestration_tests.cpp` drives `handlePathEncoding`/orchestration entry points with `copyFakeTool` + env knobs; `tests/video/encode_probe_tests.cpp` does the same for `runEncodingTasks`). The audit's function inventory identifies the untested branches: webp fallback chain and segmented encoding in `video_encode_runner.cpp`, batch-level decisions in `video_batch_execution.cpp`, parse corners in `video_info.cpp`. No bespoke harness is needed - Catch2 sections + `TempDir` + env knobs suffice.

## Goals / Non-Goals

**Goals:**
- Unit isolation for every fallback/cancel/assembly decision branch, so a regression fails with a named unit test, not a segmented e2e diff.
- Reuse: same fake tool, same env mechanism, same `TempDir` scaffolding - zero new infrastructure concepts.

**Non-Goals:**
- No monitor-thread rendering tests (`video_encoding_state.cpp`): jthread + terminal timing makes unit cost exceed value; e2e covers it indirectly.
- No refactoring of production code to expose seams: if a branch is unreachable through public entry points, that is recorded as a finding rather than papered over with `friend`/`#ifdef TEST`.
- No coverage-target chasing beyond the branch list; the number is a side effect.

## Decisions

- **Entry through public orchestration functions**: `runEncodingTasks` (with `ctx.config.verbose` / `yesToAll` / stop-signal injection), `encodeVideo` (webp/segmented paths via config + input file shape). The `video_info.cpp` parse helpers are anonymous-namespace; their branches are exercised via public `getVidTotalFrames`/`getVidTotalDurationUs` with a seeded `RuntimeContext::videoInfoCache` (the cache short-circuits before any ffprobe exec), and the input-collection predicates via `readAllVids`/`readAllVidsFromFiles` with real temp files (`ctx.toolchain.ffprobePath` pointed at a fake copy for the webp prewarm path).
- **Webp fallback triggering**: the fake tool needs to emit output whose size differs from the source to cross the target-size threshold. `ENCRO_FAKE_FFMPEG_OUTPUT_BYTES` already shapes the webp step's output size (writes a real sparse file of the requested size), so no new knob is needed. The fallback assertions read the fake tool's invocation log (`ENCRO_FAKE_TOOL_LOG_FILE`): number of encode attempts, which quality params each carried. The fallback engages only after the quality ladder exhausts: uniformly oversized output yields q=80..20 (7 attempts), and the min-quality attempt is the final one, not the second. Stale-file tests must pre-set `state.progressFilePath` - `prepareEncodeExecution` only generates a uuid path when it is unset.
- **Segmented-path assertions** also read the invocation log (segment file names, concat invocation count, audio-extraction count) rather than parsing produced media - the fake tool writes no real media, so behavioral assertions must be command-shape assertions. This matches the existing orchestration tests' style. Prerequisites: the default fake probe JSON and `kFakeProbeJson` are video-only, so audio assertions require a custom probe JSON containing an audio stream written to `ENCRO_FAKE_FFPROBE_JSON_FILE`; multi-segment runs need `ENCRO_FAKE_FFPROBE_DURATION_SECS` > 10 (default 2.0 yields exactly one segment).
- **Stop-request injection** uses the existing `ScopedStopSignalReset` + the test-side stop trigger used by current stop tests (no new mechanism).
- **Parse-corner tests are table-driven**: one TEST_CASE with Catch2 `SECTION`s per corner ("N/A", empty string, zero denominator, oversized values, missing keys) against the pure parse helpers; no fake tool involvement.
- **Sequencing after `test-suite-cleanup`**: that change deletes the 3 empty-shell tests and possibly renames files in `tests/video/`; building on the post-cleanup tree avoids conflicts. `fake_media_tool.cpp` additions (if any) are additive env knobs - no conflict with its dead-knob deletion.

## Risks / Trade-offs

- [Fallback thresholds depend on real size math the fake tool only approximates] → Assertions target the decision (attempt count, chosen quality) not byte-exact outputs; if a threshold proves untriggerable via knobs, the test records the seam finding and the branch stays covered by e2e instead of being force-fitted.
- [Batch tests become slow (fake tool process per encode)] → Existing orchestration tests spawn the same way and stay fast (<1s each); keep one process per branch, not per assertion.
- [New env knobs accumulate] → Each knob must be exercised by at least one test in this change; the dead-knob audit exists precisely to prevent orphan knobs.
- [Public-entry-only testing misses private-helper branches] → Accepted deliberately; helper-only branches reached through no public path are either dead or e2e-reachable, both fine to leave.
- [`tryParseNbFrames` uses unguarded `std::stoll`, so non-numeric input throws through the `eh::Result` contract] → Pre-existing production finding, recorded for a separate fix; this change's parse corners stay within non-throwing inputs ("N/A", empty, guarded keys).

## Migration Plan

Test-only; no migration. Rollback = revert; no production surface touched.

## Open Questions

None.
