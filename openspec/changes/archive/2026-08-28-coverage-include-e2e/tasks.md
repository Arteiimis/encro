# Tasks: coverage-include-e2e

## 1. Baseline capture

- [x] 1.1 Run `xmake coverage --e2e --summary --html` locally with current HEAD; record per-file line coverage as the before-numbers. (before, 58 e2e cases, no infra exclusion: prelude 91.89%, app_entry 76.92%, batch_execution 82.76%, encode_runner 68.61%, video_process 65.98%; TOTAL line 82.81%)

## 2. Plugin: platform-infra exclusion

- [x] 2.1 Extend the coverage ignore filter in `plugins/coverage/xmake.lua` (the regex already excluding test sources) with `src/infra/terminal.cpp`, `src/infra/stop_signal.cpp`, `src/infra/crash_runtime.cpp`, `src/infra/open_file.cpp`.
- [x] 2.2 Run locally: text report no longer lists the four files; totals shrink accordingly; all other `src/` files remain listed. (unit-only check: 4 cpp files absent, inline headers remain)

## 3. e2e pruning + harness test relocation

- [x] 3.1 Delete the 4 duplicate preview e2e cases; keep the missing-input rejection case (:2343) as the surviving preview e2e case - its exit-code assertions (exit 1 + "does not exist") stay asserted there.
- [x] 3.2 Delete the 3 duplicate picture e2e cases (survivors: `app/pipeline_picture_tests.cpp:348/:415`, `picture/picture_process_tests.cpp:485/:509`).
- [x] 3.3 Delete the 4 duplicate probe/dry-run/crf e2e cases (survivors: `video/encode_probe_tests.cpp:832/:912/:739`, `:558` for the plan table).
- [x] 3.4 Delete the 2 duplicate pack-only e2e cases (survivors: `app/pipeline_pack_only_tests.cpp`, `packer_tests.cpp:492`).
- [x] 3.5 Delete the duplicate resume-skip, `--min-vmaf` rejection, `--dry-run`+`--crf` rejection, and 64-KiB progress e2e cases (survivors: `video_process_orchestration_tests.cpp:104` + `job_state_tests.cpp:69`, `cmd_cmd_tests.cpp:758`, `cmd_cmd_tests.cpp:787`, `video_progress_parser_tests.cpp:108/:124`).
- [x] 3.6 Move the fake-ffprobe `check-input` harness self-test from `tests/e2e/encro_e2e_tests.cpp` to the unit suite's `[fake-tool]` section (same assertions, same env knob).
- [x] 3.7 Run `xmake build e2e_tests && xmake run e2e_tests` and the unit suite; confirm both green and e2e case count is 58 - 17 deleted - 1 relocated = 40; unit suite gains +1. (e2e: 40 cases / 450 assertions; unit: 633 cases / 5215 assertions, both green)

## 4. CI wiring

- [x] 4.1 Add `--e2e` to the coverage job's `xmake coverage` invocation in `.github/workflows/ci.yml`.
- [x] 4.2 Run `xmake coverage --e2e --summary --html` locally; record after-numbers for the same five files; confirm HTML at `build/coverage/html` is populated and excludes the four platform files. (after, 40 e2e cases + infra exclusion: prelude 72.97%, app_entry 74.62%, batch_execution 82.76%, encode_runner 68.61%, video_process 63.91%; TOTAL line 82.55%. Note: flat vs the proposal's ~90%+ hope - the excluded infra files were not as dilutive as estimated, and prelude/app_entry lost the main-path variants of the 17 pruned cases; the merged, filtered number is nonetheless now honest. HTML populated, 4 infra cpp absent)

## 5. CI confirmation

- [x] 5.1 Push branch; dispatch CI limited to the coverage mode (`modes: ["coverage"]`); confirm the uploaded artifact exists and its Totals reflect the merged, filtered scope. (run 33174893969 success in 15m; coverage-report artifact 518KB; CI TOTAL line 82.26% on Linux, matching local 82.55% modulo platform)
