## Why

A full test-suite audit (766 test cases, ~22.3k test lines vs ~20.8k source lines) found ~15% of test code is deletable with zero loss of regression coverage: tests that cannot fail under any implementation, tests that verify third-party library behavior (CLI11, boost::json, spdlog, fmt) instead of project code, the same behavior asserted two or three times across layers, and tests pinned to brittle implementation details (absolute help column widths, copied logger inventories). This bloat slows every test run and, worse, makes real failures harder to spot: a refactor that breaks three duplicated NVENC-chain tests reads like three bugs.

## What Changes

Test-suite-only cleanup. No production code changes; no spec requirement changes. Four action categories, all preserving every distinct behavior currently verified:

- **Delete meaningless tests (~30 cases)**: tests whose assertions cannot fail (vacuous `>= 1` checks, `CHECK_NOTHROW` on an always-catching probe, aggregate-default/enum-int checks), tests that simulate the crash handler with the test's own `std::ofstream` instead of calling crash code, a circular source-location test, an empty compile-only TU that includes nothing, a stale parser test whose named threshold no longer exists, and tests for code paths the source itself documents as unreachable.
- **Delete library-behavior tests (~12 cases)**: CLI11 `force_callback` spike block (replaced by one project-level config-store test proving a stored value does not trip `needs`/`excludes`), boost::json escaping round-trips, spdlog level-name table, fmt ANSI-rendering checks, and a 24-logger inventory copy.
- **Merge duplicates (~90 cases folded away)**: the picture workflow asserted identically at three layers (pipeline_picture / picture_process / pipeline_pack_only — keep one assertion home per behavior), skip-encode behavior tested three times, e2e cases duplicating unit coverage or splitting one scenario into two cases, struct-default re-checks duplicating an "exposes defaults" test, repeated NVENC-chain / hvc1 / banner substring pins, and near-identical families collapsed into single data-driven or SECTIONed tests (help layout invariants replace absolute column constants).
- **Relocate misplaced tests (one file created)**: the 10 `[fake-tool]` cases in `tests/video/encode_probe_tests.cpp` test the shared fake media tool, not video code — move to a dedicated unit-suite file; two `pack::execute` cases hiding in `picture_process_tests.cpp` move to `pack_execute_tests.cpp`; two `utils::findFFmpeg` cases parked in `toolchain_tests.cpp` move to `utils_tests.cpp`.

Expected result: roughly 3,000 test lines and ~130 test cases removed net (~766 → ~635 cases), unit suite runs faster, and every remaining test fails only when a real regression occurs.

## Capabilities

### New Capabilities

None. This change alters only which tests exist; it does not change any observable system behavior.

### Modified Capabilities

None. All existing capability requirements (including `cli11-native-validation`, `cli-help-layout`, `cli-help-tiering`, `portable-fake-tool`, `coverage-report`) remain fully verified after the cleanup; only redundant or vacuous verification is removed. `skip_specs: true` is set in `.openspec.yaml` accordingly.

## Impact

- **Files**: ~35 test files under `tests/` edited or deleted; one new unit-suite file `tests/fake_tool_tests.cpp`; `tests/xmake.lua` (or equivalent target file lists) updated for the deleted standalone TU and the new file. No changes under `src/`.
- **Risk**: low. Guardrails: the suite must stay green (`xmake test-parallel`) after each cluster; the `[real-ffmpeg]`/`[smoke]`/`[completion]` opt-in suites keep their current tags and skip behavior; coverage of every subprocess-orchestration source stays double-digit (only duplicate or vacuous cases are removed).
- **Verification behavior kept**: crash-runtime/stop-signal durability tests, EtaEstimator algorithm tests, e2e segment-resume/signal/persistence boundary tests, ffmpeg argument semantic pins, help tiering presence/absence tests are all explicitly out of scope for deletion.
