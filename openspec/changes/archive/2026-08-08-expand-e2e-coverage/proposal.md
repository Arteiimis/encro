## Why

The e2e suite (21 tests) covers CLI basics, the webp fake-toolchain path, and the mp4 segment/resume state machine, but the product's most important user paths have no end-to-end coverage: mp4 (the default output format) is never verified against a real ffmpeg, the picture pipeline has zero e2e tests, `-j` concurrency is always exercised as `-j 1`, and interruption (Ctrl+C / kill) — the exact scenario the stop-signal and job-state machinery was built for — is never tested. As a result, "the binary works for real users" is only weakly evidenced outside the webp path.

## What Changes

All changes are test-only (tests/e2e, tests/xmake.lua); no product behavior changes.

- **Interruption e2e infrastructure**: add an async process runner (`runEncroAsync` returning a `RunningProcess` handle) with process-group isolation on Windows (`CREATE_NEW_PROCESS_GROUP` via a boost::process v1 handler initializer) so `GenerateConsoleCtrlEvent` can deliver Ctrl+C only to the encro group; POSIX uses `kill(SIGINT)`. Handle exposes `wait(timeout)`, `sendCtrlC()`, `terminate()`.
- **Interruption tests**: (a) graceful Ctrl+C mid-encode → exit code 130, state file saved, resume completes; (b) hard kill (terminate) → state file intact, resume continues from last completed task; (c) cancel before any task started → state file removed (existing `maybeRemoveUnstartedCanceledJobState` path).
- **mp4 real-ffmpeg smoke tests**: generate a real test source with audio, encode mp4 end-to-end, probe output (codec, audio stream), plus a segment/resume smoke with real ffmpeg.
- **Picture-mode e2e tests**: `-t pic` compress flow (`-c/-q`) and folder-summary (`-s`) with the fake toolchain, including failure and resume paths.
- **Concurrency e2e test**: `-j>1` with `ENCRO_FAKE_FFMPEG_DELAY_MS` to prove parallel encodes actually overlap and all outputs land.
- **Partial-failure test**: multi-input run where one encode fails (`ENCRO_FAKE_FFMPEG_FAIL_MATCH`) → non-zero exit, successful tasks still encoded, state records per-task status.
- **Fake toolchain enhancements** (only what the tests above need): per-output-file size control (`ENCRO_FAKE_FFMPEG_OUTPUT_BYTES` already exists — wire it into tests), input-existence validation toggle for ffprobe, and Ctrl+C default-termination behavior is inherited naturally (fake tool has no handler).
- **Overwrite/interactive prompt e2e**: run without `-y` against an existing output → verify prompt text and that `-w/--overwrite` or `-y` proceeds (non-TTY input path only; interactive stdin is out of scope).

## Capabilities

### New Capabilities

None — this change is test infrastructure only; no product behavior changes, so no spec deltas are introduced (`skip_specs: true` declared in `.openspec.yaml`).

### Modified Capabilities

None.

## Impact

- `tests/e2e/e2e_test_utils.{h,cpp}` — async runner, `RunningProcess`, process-group creation, signal delivery, `SKIP`-able control-console guard for CI.
- `tests/e2e/encro_e2e_tests.cpp` — new TEST_CASEs for interruption, mp4 smoke, picture, concurrency, partial failure, overwrite prompt.
- `tests/e2e/fake_media_tool.cpp` — optional input-existence check for ffprobe; no behavioral change to existing env-var contract.
- `xmake.lua` — no new targets; e2e_tests already depends on encro + encro_e2e_tool.
- Risk: Windows console-event delivery requires a console shared with the child; tests must `SKIP` gracefully when unavailable (same pattern as `[real-ffmpeg]`).
