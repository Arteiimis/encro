# Tasks: test-suite-cleanup

## 1. Redundant TEST_CASE deletions

- [x] 1.1 Delete the duplicate timestamp test in `tests/app/app_entry_tests.cpp` (survivor: the first test in the same file).
- [x] 1.2 Delete 3 of the 4 empty-shell tests in `tests/video/video_batch_execution_tests.cpp`, keeping one compile-smoke case (survivor: the remaining smoke test).
- [x] 1.3 Delete the XPSNR round-trip and metric-differentiation duplicates in `tests/video/probe_cache_tests.cpp` (survivors: round-trip test at :90 and key test at :10; fold the deleted test's comment value into the survivor if non-trivial).
- [x] 1.4 Delete the default-config setup test and the "cleanup does not delete current log file" test in `tests/logging_file_mgmt_test.cpp` (survivors: :158 timestamped-path test, :198 retention test).
- [x] 1.5 Delete the "named spinner function" and "delegates packSourceEntries" tests in `tests/packer_tests.cpp` (survivors: :193 archive-and-progress test, :41 groupPackFiles test).
- [x] 1.6 Delete the non-recursive `packAllFilesInDirectory` test in `tests/pack_service_mock_tests.cpp` (survivor: `packer_tests.cpp:446`).
- [x] 1.7 Run `xmake test-report`; confirm green and note the new TEST_CASE count (11 fewer). (632 cases, 5055 assertions, green)

## 2. Helper hoisting into tests/test_utils.h

- [x] 2.1 Add shared `registerCapturingLogger` to `tests/test_utils.h`; replace the 7 verbatim copies (logging infra/error-context/scoped-timer/snapshot, stop_signal, task_executor, job_state) and reduce the json variant to a thin wrapper.
- [x] 2.2 Add `writeSizedFile` to `tests/test_utils.h`; delete the 11 local file-creation helpers (4 sparse variants, 2 `createBinaryFile`, 2 `createFile`, `createSizedFile`, `createTempFile` ×2) and switch callers.
- [x] 2.3 Delete the 3 duplicate file-read helpers (`logging_crash_integration_test.cpp`, `infra/stop_signal_tests.cpp`, local `readTextFile` in `encro_e2e_tests.cpp`) in favor of `testutils::readTextFile`.
- [x] 2.4 Delete `cmd_cmd_tests.cpp`'s local `ScopedEnvVar` (use `testutils::ScopedEnvVar`) and hoist the `parseArgs`/`findHelpLine` pair shared with `cmd_help_tiering_tests.cpp`.
- [x] 2.5 Add `countOccurrences` and replace the 8 hand-rolled counting loops in `logging_scoped_timer_test.cpp`; extract a local scaffold factory for the 5 `runEncodingTasks` setups in `tests/video/encode_probe_tests.cpp` (tests unchanged, scaffolding shared).
- [x] 2.6 Replace the hand-rolled `makeTestDir` copies in `logging_summary_test.cpp` / `logging_file_mgmt_test.cpp` with `TempDir`.
- [x] 2.7 Run `xmake test-report`; confirm green. (632 cases, 5211 assertions, green)

## 3. e2e harness overlap + dead code

- [x] 3.1 Merge `e2e::writeTextFile`, `e2e::listZipEntries`, and the local `listRegularFiles` in `encro_e2e_tests.cpp` onto the `test_utils.h` equivalents; keep `e2e::ScopedEnvironmentOverrides` semantics untouched.
- [x] 3.2 Delete `RunningProcess::id()` from `tests/e2e/e2e_test_utils.{h,cpp}`; sink the `fakeMediaToolBinaryPath` declaration into the .cpp.
- [x] 3.3 Delete the `ENCRO_FAKE_FFMPEG_PROGRESS_FRAMES` knob in `tests/e2e/fake_media_tool.cpp` (hardcode frame=10).
- [x] 3.4 Run `xmake build e2e_tests && xmake run e2e_tests`; confirm green. (58 cases, 570 assertions, green)

## 4. Renames + final verification

- [x] 4.1 `git mv` the 14 `*_test.cpp` files to `*_tests.cpp`; reconfigure and rebuild both test targets.
- [x] 4.2 Run `xmake test-parallel` (full unit + e2e, sharded); confirm green. (690 cases / 7708 assertions across 12 shards)
- [x] 4.3 Run `xmake tidy`; confirm no new findings attributable to moved/hoisted helpers. (one performance-unnecessary-value-param on the new reregisterLogger fixed; all other warnings pre-existing baseline)
