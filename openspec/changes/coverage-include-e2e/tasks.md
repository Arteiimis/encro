# Tasks: coverage-include-e2e

## 1. Baseline capture

- [ ] 1.1 Run `xmake coverage --e2e --summary --html` locally with current HEAD; record per-file line coverage for `app/prelude.cpp`, `app/app_entry.cpp`, `video/video_batch_execution.cpp`, `video/video_encode_runner.cpp`, `video/video_process.cpp` as the before-numbers (note: local run may predate pruning; numbers are directional).

## 2. Plugin: platform-infra exclusion

- [ ] 2.1 Extend the coverage ignore filter in `plugins/coverage/xmake.lua` (the regex already excluding test sources) with `src/infra/terminal.cpp`, `src/infra/stop_signal.cpp`, `src/infra/crash_runtime.cpp`, `src/infra/open_file.cpp`.
- [ ] 2.2 Verify locally: text report no longer lists the four files; totals shrink accordingly; all other `src/` files remain listed.

## 3. e2e pruning + harness test relocation

- [ ] 3.1 Delete the 4 duplicate preview e2e cases (survivor unit tests: `preview/preview_process_tests.cpp` equivalents named in the audit table); keep the missing-input rejection case's exit-code assertion by folding it into the surviving preview e2e case if not already asserted there.
- [ ] 3.2 Delete the 3 duplicate picture e2e cases (survivors: `app/pipeline_picture_tests.cpp:348/:415`, `picture/picture_process_tests.cpp:485/:509`).
- [ ] 3.3 Delete the 4 duplicate probe/dry-run/crf e2e cases (survivors: `video/encode_probe_tests.cpp:832/:912/:739`, `:558` for the plan table).
- [ ] 3.4 Delete the 2 duplicate pack-only e2e cases (survivors: `app/pipeline_pack_only_tests.cpp`, `packer_tests.cpp:492`).
- [ ] 3.5 Delete the duplicate resume-skip, `--min-vmaf` rejection, `--dry-run`+`--crf` rejection, and 64-KiB progress e2e cases (survivors: `video_process_orchestration_tests.cpp:104` + `job_state_tests.cpp:69`, `cmd_cmd_tests.cpp:758`, `cmd_cmd_tests.cpp:787`, `video_progress_parser_tests.cpp:108/:124`).
- [ ] 3.6 Move the fake-ffprobe `check-input` harness self-test from `tests/e2e/encro_e2e_tests.cpp` to the unit suite's `[fake-tool]` section (same assertions, same env knob).
- [ ] 3.7 Run `xmake build e2e_tests && xmake run e2e_tests` and the unit suite; confirm both green and e2e case count is 58 - 17 deleted - 1 relocated = 40; unit suite gains +1.

## 4. CI wiring

- [ ] 4.1 Add `--e2e` to the coverage job's `xmake coverage` invocation in `.github/workflows/ci.yml`.
- [ ] 4.2 Run `xmake coverage --e2e --summary --html` locally; record after-numbers for the same five files; confirm HTML at `build/coverage/html` is populated and excludes the four platform files.

## 5. CI confirmation

- [ ] 5.1 Push branch; dispatch CI limited to the coverage mode (`modes: ["coverage"]`); confirm the uploaded artifact exists and its Totals reflect the merged, filtered scope.
