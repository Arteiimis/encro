# Proposal: port-win-gated-tests-to-fake-tool

## Why

CI runs on Linux, but 45 of 692 unit `TEST_CASE`s are wrapped in `#if defined(_WIN32)` because they impersonate ffmpeg/ffprobe with inline `cmd.exe` batch scripts. Those silent exclusions sit exactly on the process-spawning orchestration layers (`preview`, `picture`, `video`, app pipeline), which is why the latest CI coverage report shows those modules at the bottom (src line coverage 70.64% overall; `preview_process.cpp` 5.4%, `picture_compress.cpp` 3.7%, `video_process.cpp` 10.4%). The cost is not cosmetic: Linux builds ship with ~45 orchestration tests never executed anywhere in CI, so platform-specific regressions in these flows reach master undetected.

## What Changes

- Promote the already-proven fake-tool binary pattern to shared infrastructure: extract the hand-rolled `ScopedEnvVar` / `copyFakeTool` helpers (currently duplicated inside `preview_process_tests.cpp` and `encode_probe_tests.cpp`) into `tests/test_utils.h`, making them portable (POSIX `setenv`/`unsetenv`, executable-suffix handling, argv[0]-role copies of `FAKE_TOOL_EXE_PATH`).
- Port every `_WIN32`-gated test case (~45 across `picture_compress_tests.cpp`, `picture_process_tests.cpp`, `pipeline_picture_tests.cpp`, `preview_process_tests.cpp`, `video_process_orchestration_tests.cpp`) from batch-script fakes to the native fake tool (`tests/e2e/fake_media_tool.cpp`) driven by environment variables; remove the platform guards.
- Align `tests/video/encode_probe_tests.cpp` (already on the binary-copy pattern, not part of the ~45 count) onto the shared helpers instead of its private duplicates.
- Extend `fake_media_tool.cpp` with the few missing behaviors currently expressed only as stateful batch scripts: per-invocation scheduling of delay and exit code (blocking-yet-succeeding calls, delayed failure), and suppression of the optional `out_time_us` progress field; exact command-line assertions reuse the existing invocation log rather than any new mechanism.
- No changes to production code under `src/`.
- Verification requirement: after porting, the Linux branch SHALL be verified locally through the system WSL (build + run the unit suite) before pushing; CI confirms afterwards.

## Capabilities

### New Capabilities
- `portable-fake-tool`: contract for the cross-platform fake ffmpeg/ffprobe infrastructure used by the unit suite - environment-variable-driven behaviors, role-by-argv[0] selection, and the guarantee that process-spawning unit tests are not compiled away on any supported platform.

### Modified Capabilities

## Impact

- `tests/test_utils.h`: add portable `ScopedEnvVar` + fake-tool copy/locate helpers.
- `tests/picture/picture_compress_tests.cpp`, `tests/picture/picture_process_tests.cpp`, `tests/app/pipeline_picture_tests.cpp`, `tests/preview/preview_process_tests.cpp`, `tests/video/video_process_orchestration_tests.cpp`: remove guards, replace scripts.
- `tests/video/encode_probe_tests.cpp`: switch duplicated fake-tool helpers to the shared ones (no gated cases there; already on the target pattern).
- `tests/e2e/fake_media_tool.cpp`: add missing behavior knobs (counter-based failure sequencing).
- `xmake.lua`: no change needed (the `FAKE_TOOL_EXE_PATH` define wiring already exists).
- CI effect: unit suite grows by the previously-gated cases on Linux; src line coverage expected to rise materially (~70% toward mid/high 80s); debug/release jobs gain the same tests.
