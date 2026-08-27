# Tasks: port-win-gated-tests-to-fake-tool

## 1. Shared test helpers

- [x] 1.1 Add portable `testutils::ScopedEnvVar` to `tests/test_utils.h` (`_putenv_s` on Windows, `setenv`/`unsetenv` with prior-value capture elsewhere); delete the private duplicate from `preview_process_tests.cpp`.
- [x] 1.2 Add portable `testutils::copyFakeTool(dir, name)` to `tests/test_utils.h` (`.exe` suffix on Windows only, hard fail when `FAKE_TOOL_EXE_PATH` undefined); delete the private duplicate from `preview_process_tests.cpp`; switch `video/encode_probe_tests.cpp` onto the shared helper.

## 2. Fake tool behavior gaps

- [x] 2.1 Inventory every `_WIN32`-gated case across the five suites; map each batch-script behavior to an existing env knob or a named gap (budget sequencing, exact-content bodies) recorded in the change notes.
- [x] 2.2 Add unit cases first for the two new knobs, then implement them in `tests/e2e/fake_media_tool.cpp`: (a) per-invocation scheduling of delay and exit code keyed by a caller-provided counter file (covers cancel-mid-batch blocking-yet-succeeding calls and delayed-failure exit 130 flows); (b) progress `out_time_us` field suppression; while there, make progress-file emission create missing parent directories.

## 3. Port suites (D4 order)

- [x] 3.1 Pilot `picture_compress_tests.cpp`: drop all guards, replace the three batch scripts with fake-tool invocations (success / exit-code failure via `ENCRO_FAKE_FFMPEG_EXIT_CODE` with partial output side effect / budget-based retry failure); assertions preserved, Windows-only CRT usages removed.
- [x] 3.2 `preview_process_tests.cpp` and `video/encode_probe_tests.cpp`: remove remaining `_WIN32` gates now that shared helpers exist; confirm no `.exe` assumptions remain outside `copyFakeTool`.
- [x] 3.3 `picture_process_tests.cpp` and `app/pipeline_picture_tests.cpp`: replace counter-path batch scripts with the per-invocation schedule knob; adapt argument assertions to the tab-separated `ENCRO_FAKE_TOOL_LOG_FILE` format (drop `%*` echo scripts).
- [x] 3.4 `video/video_process_orchestration_tests.cpp`: replace progress-emission and multi-line-body scripts with existing progress knobs plus the `out_time_us` suppression knob where a case asserts segment-end fallback; fixture-file echo where line content matters; relax pure-plausibility content assertions per D3 default stance and list any relaxed assertion in the change notes.
- [x] 3.5 Sweep `rg -n "_WIN32" tests/` : zero platform-gated process-spawning test cases remain; keep (with explanatory comment) only genuinely Windows-specific contracts if any surface.

## 4. Verification

- [x] 4.1 Windows: `xmake build encro encro_e2e_tool tests e2e_tests` then full unit suite green locally (clang-cl).
- [x] 4.2 WSL (mandated Linux pre-check): configure/build/run the unit suite under system WSL until green; capture failing deltas as fixes, not gate re-introductions.
- [x] 4.3 `xmake fmt` clean; run touched suites under `xmake test-report --tag="[picture]"` etc. where useful.
- [ ] 4.4 Commit (implementation + its tests + this file's checkboxes in ONE conventional-commit), push, confirm ubuntu debug/release jobs green; download coverage artifact and compare bottom modules against baseline run 33002646675: no orchestration source file below double-digit line coverage (`preview_process.cpp` 5.4%, `picture_compress.cpp` 3.7%, `video_process.cpp` 10.4% all above single digits); src overall well above 70.64%.
