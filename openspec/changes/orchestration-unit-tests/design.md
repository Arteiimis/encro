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

- **Entry through public orchestration functions**: `runEncodingTasks` (with `ctx.config.verbose` / `yesToAll` / stop-signal injection), `encodeVideo` (webp/segmented paths via config + input file shape), `readAllVids`/parse helpers. If `runEncodingWithoutProgress` is only reachable via `runEncodingTasks` + verbose config, tests go through that public route.
- **Webp fallback triggering**: the fake tool needs to emit output whose size differs from the source to cross the target-size threshold. Reuse `ENCRO_FAKE_FFMPEG_OUTPUT_BYTES` / `FAIL_OUTPUT_BYTES` if they already shape output size (audit: both knobs exist and are live); add one knob only if neither controls the webp step's output size. The fallback assertions read the fake tool's invocation log (`ENCRO_FAKE_TOOL_LOG_FILE`): number of encode attempts, which quality params each carried.
- **Segmented-path assertions** also read the invocation log (segment file names, concat invocation count, audio-extraction count) rather than parsing produced media - the fake tool writes no real media, so behavioral assertions must be command-shape assertions. This matches the existing orchestration tests' style.
- **Stop-request injection** uses the existing `ScopedStopSignalReset` + the test-side stop trigger used by current stop tests (no new mechanism).
- **Parse-corner tests are table-driven**: one TEST_CASE with Catch2 `SECTION`s per corner ("N/A", empty string, zero denominator, oversized values, missing keys) against the pure parse helpers; no fake tool involvement.
- **Sequencing after `test-suite-cleanup`**: that change deletes the 3 empty-shell tests and possibly renames files in `tests/video/`; building on the post-cleanup tree avoids conflicts. `fake_media_tool.cpp` additions (if any) are additive env knobs - no conflict with its dead-knob deletion.

## Risks / Trade-offs

- [Fallback thresholds depend on real size math the fake tool only approximates] → Assertions target the decision (attempt count, chosen quality) not byte-exact outputs; if a threshold proves untriggerable via knobs, the test records the seam finding and the branch stays covered by e2e instead of being force-fitted.
- [Batch tests become slow (fake tool process per encode)] → Existing orchestration tests spawn the same way and stay fast (<1s each); keep one process per branch, not per assertion.
- [New env knobs accumulate] → Each knob must be exercised by at least one test in this change; the dead-knob audit exists precisely to prevent orphan knobs.
- [Public-entry-only testing misses private-helper branches] → Accepted deliberately; helper-only branches reached through no public path are either dead or e2e-reachable, both fine to leave.

## Migration Plan

Test-only; no migration. Rollback = revert; no production surface touched.

## Open Questions

None.
