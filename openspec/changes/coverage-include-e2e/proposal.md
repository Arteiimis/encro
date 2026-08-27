# Proposal: coverage-include-e2e

## Why

The coverage report counts unit tests only, while the 58-case e2e suite drives the real binary through the exact layers the report shows as blind: the latest local run puts `app_entry.cpp` at 19.6% and `prelude.cpp` at 0% even though e2e executes both on every case, and the video orchestration layer at 35-74% though e2e exercises its segment-failure/cancel/resume paths. Half of the reported gap is a measurement artifact, not a test gap. Separately, an audit found 17 of the 58 e2e cases re-assert, with the same fake tool and the same injections, behavior already covered in-process by unit tests (plus 1 misplaced harness self-test) - they add process-startup cost and one redundant "exit code == 0" each, and would dilute (not inflate) the merged report. Platform-bound code (console API, signal handlers, crash handlers, `ShellExecute`) is structurally untestable in-process and permanently drags the number down regardless of test effort.

## What Changes

- Merge e2e coverage into the report: CI's coverage job (`ci.yml`) adds `--e2e` to `xmake coverage`; the plugin's existing `--e2e` path (fake-tool e2e run under instrumentation, `e2e-%p.profraw` merged into `all.profdata`) becomes the standard CI configuration. Local `xmake coverage --e2e` reproduces it. The HTML report keeps its single-binary generation (tests.exe contains all `src/` + `tests/` sources; e2e counters merge into the same profile).
- Exclude platform-bound infrastructure sources from the report via the llvm-cov ignore filter (extending the existing test-sources exclusion): `src/infra/terminal.cpp`, `src/infra/stop_signal.cpp`, `src/infra/crash_runtime.cpp`, `src/infra/open_file.cpp`. `app/prelude.cpp` and `app/app_entry.cpp` are NOT excluded - they are expected to rise via e2e inclusion; if they remain low after merge, that is real signal for orchestration-unit-tests.
- Prune the 17 duplicated e2e cases (same fake tool, same injection, equivalent assertions as named unit tests): preview group 4/5, picture group 3/3, probe/dry-run/crf group 4/5, pack-only group 2/2, resume-skip 1, CLI rejection 2, 64-KiB progress 1. The 12 "complementary" cases (exit-code/CLI-glue value) and 28 unique-coverage cases (real process boundaries, Ctrl+C, segmented resume orchestration, real-ffmpeg smoke) are kept.
- Relocate the misplaced harness self-test (fake ffprobe `check-input`, currently in `encro_e2e_tests.cpp`) to the unit suite's `[fake-tool]` section, where the other fake-tool behavior tests live.
- Expected effect: src line coverage rises from 82.79% toward ~90%+ with the exclusions making the remaining number honest; e2e wall time shrinks by the 17 pruned process startups.

## Capabilities

### New Capabilities

- `coverage-report`: contract for the `xmake coverage` command and its CI integration - what the report counts (unit + e2e suites merged into one profile), what it excludes (test sources, platform-bound infra sources), its output forms (text summary, per-file table, optional HTML), and the artifact CI uploads.

### Modified Capabilities

## Impact

- `.github/workflows/ci.yml`: coverage job gains `--e2e` (one word); nothing else.
- `plugins/coverage/xmake.lua`: add the platform-infra ignore regex alongside the existing test-source filter; no new options needed (`--e2e`, `--html`, `--summary` already exist).
- `tests/e2e/encro_e2e_tests.cpp`: remove 17 cases, move 1 harness self-test; `tests/unit` side gains nothing (the 17 duplicates' unit counterparts already exist and stay).
- No `src/` changes. Coverage numbers change by definition (that is the point); no production behavior changes.
- Depends on: none, but landing after `test-suite-cleanup` avoids touching files that change there (`encro_e2e_tests.cpp` is touched by both).
