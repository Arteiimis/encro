# Proposal: test-suite-cleanup

## Why

A read-only audit of `tests/` (59 files, ~18K lines, 701 TEST_CASEs) found ~550-600 lines of dead weight: ~10 copy-paste TEST_CASEs asserting already-covered behavior, 8 verbatim copies of `registerCapturingLogger`, 10 near-identical file-creation helpers, 3 duplicate file-read helpers, two byte-identical local helper pairs, and exactly two pieces of dead code. This is noise: every future orchestration test lands next to scaffolding copies, and duplicated helpers drift apart (one file even re-implements `readTextFile` while already including `test_utils.h`). Cleaning first also shrinks the diff surface for the two planned follow-ups (coverage-include-e2e, orchestration-unit-tests).

## What Changes

- Delete redundant TEST_CASEs (~14 total; every deleted assertion is a strict subset of a surviving test in the same or a sibling file):
  - `tests/app/app_entry_tests.cpp`: second timestamp-format test (copy of the first).
  - `tests/video/video_batch_execution_tests.cpp`: 3 of 4 empty-shell "types compile / helpers extracted" tests (identical empty-map bodies, two with no meaningful assertion).
  - `tests/video/probe_cache_tests.cpp`: XPSNR round-trip duplicate (subset of the SSIM/VMAF round-trip) and metric-differentiation duplicate (same branch as the key test).
  - `tests/logging_file_mgmt_test.cpp`: default-config setup test (strict subset of the timestamped-path test) and the "cleanup keeps current file" test (equivalent assertion already ends the retention test).
  - `tests/packer_tests.cpp`: "named spinner function" scaffolding test (subset of the archive-and-progress test) and the "delegates to named function" test (same setup/assertion as `groupPackFiles keeps source directories intact`, delegation invisible to assertions).
  - `tests/pack_service_mock_tests.cpp`: "packAllFilesInDirectory respects non-recursive" (same public function, same branch, near-identical name as `packer_tests.cpp`'s version, which asserts entry names more precisely).
- Hoist duplicated helpers into `tests/test_utils.h`:
  - `registerCapturingLogger` (8 copies: logging infra/error-context/scoped-timer/snapshot/json, stop_signal, task_executor, job_state; one already carries a "hoist when a fifth copy appears" comment).
  - File-creation helpers (10 copies: 4 sparse-sized variants, 2 byte-identical `createBinaryFile`, 2 byte-identical `createFile`, plus `createSizedFile`/`createTempFile`) behind one `writeSizedFile`.
  - 3 `readFileContent`-style helpers replaced by `testutils::readTextFile`.
  - `cmd_cmd_tests.cpp` local `ScopedEnvVar` (line-equivalent to `testutils::ScopedEnvVar`) and the duplicated `parseArgs`/`findHelpLine` pair shared with `cmd_help_tiering_tests.cpp`.
  - A 5-line `countOccurrences` helper for the 6 hand-rolled counting loops in `logging_scoped_timer_test.cpp`; a local scaffold factory for the 6 ~20-line setups in `encode_probe_tests.cpp` `runEncodingTasks` tests (tests kept, scaffolding deduplicated).
  - Merge the 4 `e2e_test_utils` items that duplicate `test_utils.h` (`writeTextFile`, `listZipEntries`, `listRegularFiles`, and the env-override pair, keeping the e2e variant's restore-to-unset semantics).
- Delete dead code (2 items, the audit's only true dead code):
  - `RunningProcess::id()` declaration + definition in `tests/e2e/e2e_test_utils.{h,cpp}` (zero call sites).
  - `ENCRO_FAKE_FFMPEG_PROGRESS_FRAMES` knob in `tests/e2e/fake_media_tool.cpp` (no test references it; hardcode frame=10; progress emission stays env-configurable via the PROGRESS_PAD / PROGRESS_NO_END_TIME knobs, so the portable-fake-tool mechanism contract is untouched).
- Rename the 14 legacy `*_test.cpp` files to `*_tests.cpp` (mechanical, no content change).
- Switch the hand-rolled `makeTestDir` copies in `logging_summary_test.cpp` / `logging_file_mgmt_test.cpp` to the guarded `TempDir`.
- No changes under `src/`. No test behavior is lost: coverage numbers for surviving tests are unchanged.

## Capabilities

### New Capabilities

### Modified Capabilities

(No capability deltas: this is a pure test-suite refactor - no production behavior, no fake-tool mechanism contract, and no spec-level requirement changes. Declared `skip_specs: true`.)

## Impact

- `tests/**` only (~550-600 lines removed net; TEST_CASE count drops by ~14; no assertion coverage lost - each deletion is a subset of a surviving test).
- `tests/test_utils.h` grows by the hoisted helpers (~40 lines) while 20+ local copies disappear.
- `tests/e2e/e2e_test_utils.{h,cpp}`, `tests/e2e/fake_media_tool.cpp`: dead-code removal only.
- `src/`: untouched. Build system: only file renames (xmake wildcard globs already pick up `tests/**`; verify both unit and e2e targets still configure).
- Risk: low - all changes are mechanically verifiable by the suites themselves (unit + e2e must pass with identical assertion semantics; `xmake test-report` and `xmake test-parallel` green).
