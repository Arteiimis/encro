## 1. The status mapping and the funnel (tests first)

- [x] 1.1 Add `appentry::runStatus(int exitCode, bool stopRequested) -> std::string` to `src/app/app_entry.h` with the four classes (`0` → `success` regardless of the stop fact, `kCanceledExitCode` → `interrupted`, other non-zero → `interrupted` when a stop is pending, else `failed`) and a unit case pinning all four; verify the case fails before the function exists (red first)
- [x] 1.2 Add `StartupContext.loggingActive` in `src/app/prelude.{h,cpp}`, set in the same branch that calls `setupLogging`; verify `rg -n "setupLogging|loggingActive" src/app/prelude.cpp` shows one condition and one assignment
- [x] 1.3 Turn `failWithHint` into `failRun(message, showHelpHint) -> int` (error line, `LOG_ERROR`, optional help hint, return `1`); verify with `rg -n "failWithHint|failRun" src/app` that no `failWithHint` remains and every `failRun` call either returns the code or is followed by its function's failure return (`buildAppConfig`/`ensureToolchainReady`)
- [x] 1.4 Move the teardown into `run` as the single funnel: every dispatch branch assigns one `exitCode` local and falls through (no direct returns after dispatch; help/version included, where `loggingActive` is false), gate on `startup.loggingActive`, keep the order `printLogHint` (failure only) → `logRunSummary(buildSummary(&ctx, runStatus(exitCode, stopsignal::isStopRequested())))` → `shutdown`, and return the exit code; verify `xmake build encro`, `xmake run encro --version` behaves as before (no log file, no summary), and a parse-error run still prints the hint and writes a `failed` summary

## 2. Remove the three copies

- [x] 2.1 `runPreview`: drop its `printLogHint`/`logRunSummary`/`shutdown` block and return the exit code (its failure path uses `failRun`; the funnel reports the canceled case as `interrupted` because the exit code is non-zero and a stop is pending); verify the `[preview]` cases and the preview e2e flows pass
- [x] 2.2 `runAppPipeline`: same removal, with the status now coming from the funnel; verify the pipeline's `interrupted` e2e section (`tests/e2e/encro_e2e_tests.cpp:1777`) stays green
- [x] 2.3 The `organize`/`config`/`completion` blocks in `run`: keep their exit codes, drop their `printLogHint` calls; verify a failing `encro config get <unknown-key>` still prints the hint and now also writes a summary
- [x] 2.4 Verify nothing below the funnel tears down: `rg -n "printLogHint|logRunSummary|logging::shutdown" src/` lists only `src/app/app_entry.cpp`'s funnel and the logging module itself

## 3. Tests

- [x] 3.1 Add the e2e case **"a subcommand run ends with a summary record"**: run `config set crf 23` with `--log-json` (before the subcommand) against a temp `ENCRO_CONFIG` and the test log root, poll `lastNdjsonRecord` through `waitUntil`, and assert the last record carries `summary` with `status` `success`; verify it fails against the pre-change binary (no summary record) and passes after
- [x] 3.2 Record the declared canceled-preview fix's verification honestly: the rule is pinned by the `runStatus` unit case (1.1), which takes the stop fact as a parameter, so no canceled preview needs staging; note here that the harness has no fake-toolchain preview cancel path (only the `[real-ffmpeg]` smoke) and that the interrupted pipeline e2e (`tests/e2e/encro_e2e_tests.cpp:1777`) now exercises `runStatus(130, …)` end-to-end
- [x] 3.3 Verify the untouched suites still pass: `tests/logging_summary_tests.cpp`, the quiet-mode summary assertions (`tests/e2e/encro_e2e_tests.cpp:581-672`), and the log-hint assertions

## 4. Verification & commits

- [x] 4.1 Run `xmake test-report` (full unit suite) and confirm zero failures
- [x] 4.2 Run `xmake build e2e_tests && xmake run e2e_tests` and confirm the stop/resume and summary flows behave
- [ ] 4.3 Run `xmake test-parallel` and confirm every shard reports its assigned case count
- [x] 4.4 Run `xmake fmt` before committing
- [x] 4.5 Commit the planning artifacts first as their own `docs:` commit (proposal, design, tasks, `.openspec.yaml`), then implementation + tests + ticked `tasks.md` in one commit (`fix:`, since the change corrects a spec violation and its declared status behavior; English, conventional, subject < 72 chars, body wrapped at 80); verify with `git log --oneline -2` that the split is docs-then-change
