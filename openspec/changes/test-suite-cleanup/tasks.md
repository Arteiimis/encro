# Tasks: test-suite-cleanup

## 1. Redundant TEST_CASE deletions

- [ ] 1.1 Delete the duplicate timestamp test in `tests/app/app_entry_tests.cpp` (survivor: the first test in the same file).
- [ ] 1.2 Delete 3 of the 4 empty-shell tests in `tests/video/video_batch_execution_tests.cpp`, keeping one compile-smoke case (survivor: the remaining smoke test).
- [ ] 1.3 Delete the XPSNR round-trip and metric-differentiation duplicates in `tests/video/probe_cache_tests.cpp` (survivors: round-trip test at :61 and key test at :13; fold the deleted test's comment value into the survivor if non-trivial).
- [ ] 1.4 Delete the default-config setup test and the "cleanup does not delete current log file" test in `tests/logging_file_mgmt_test.cpp` (survivors: :158 timestamped-path test, :198 retention test).
- [ ] 1.5 Delete the "named spinner function" and "delegates packSourceEntries" tests in `tests/packer_tests.cpp` (survivors: :193 archive-and-progress test, :41 groupPackFiles test).
- [ ] 1.6 Delete the non-recursive `packAllFilesInDirectory` test in `tests/pack_service_mock_tests.cpp` (survivor: `packer_tests.cpp:446`).
- [ ] 1.7 Run `xmake test-report`; confirm green and note the new TEST_CASE count (~14 fewer).

## 2. Helper hoisting into tests/test_utils.h

- [ ] 2.1 Add shared `registerCapturingLogger` to `tests/test_utils.h`; replace the 7 verbatim copies (logging infra/error-context/scoped-timer/snapshot, stop_signal, task_executor, job_state) and reduce the json variant to a thin wrapper.
- [ ] 2.2 Add `writeSizedFile` to `tests/test_utils.h`; delete the 10 local file-creation helpers (4 sparse variants, 2 `createBinaryFile`, 2 `createFile`, `createSizedFile`, `createTempFile`) and switch callers.
- [ ] 2.3 Delete the 3 duplicate file-read helpers (`logging_crash_integration_test.cpp`, `infra/stop_signal_tests.cpp`, local `readTextFile` in `encro_e2e_tests.cpp`) in favor of `testutils::readTextFile`.
- [ ] 2.4 Delete `cmd_cmd_tests.cpp`'s local `ScopedEnvVar` (use `testutils::ScopedEnvVar`) and hoist the `parseArgs`/`findHelpLine` pair shared with `cmd_help_tiering_tests.cpp`.
- [ ] 2.5 Add `countOccurrences` and replace the 6 hand-rolled counting loops in `logging_scoped_timer_test.cpp`; extract a local scaffold factory for the 6 `runEncodingTasks` setups in `tests/video/encode_probe_tests.cpp` (tests unchanged, scaffolding shared).
- [ ] 2.6 Replace the hand-rolled `makeTestDir` copies in `logging_summary_test.cpp` / `logging_file_mgmt_test.cpp` with `TempDir`.
- [ ] 2.7 Run `xmake test-report`; confirm green.

## 3. e2e harness overlap + dead code

- [ ] 3.1 Merge `e2e::writeTextFile`, `e2e::listZipEntries`, and the local `listRegularFiles` in `encro_e2e_tests.cpp` onto the `test_utils.h` equivalents; keep `e2e::ScopedEnvironmentOverrides` semantics untouched.
- [ ] 3.2 Delete `RunningProcess::id()` from `tests/e2e/e2e_test_utils.{h,cpp}`; sink the `fakeMediaToolBinaryPath` declaration into the .cpp.
- [ ] 3.3 Delete the `ENCRO_FAKE_FFMPEG_PROGRESS_FRAMES` knob in `tests/e2e/fake_media_tool.cpp` (hardcode frame=10).
- [ ] 3.4 Run `xmake build e2e_tests && xmake run e2e_tests`; confirm green.

## 4. Renames + final verification

- [ ] 4.1 `git mv` the 14 `*_test.cpp` files to `*_tests.cpp`; reconfigure and rebuild both test targets.
- [ ] 4.2 Run `xmake test-parallel` (full unit + e2e, sharded); confirm green.
- [ ] 4.3 Run `xmake tidy`; confirm no new findings attributable to moved/hoisted helpers.
