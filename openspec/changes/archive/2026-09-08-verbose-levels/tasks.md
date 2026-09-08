## 1. Flag surface

- [x] 1.1 Add failing CLI tests: `-vv` yields verbosity level 2, `--debug` yields level 2, single `-v` yields level 1, `--quiet` parses and is exposed to the parse result, and `-q` remains image quality (no short alias for quiet). Then implement occurrence counting and the new flags in `src/cmd/cmd.{h,cpp}` (`--quiet` in the visible General tier, `--debug` in the hidden tier per design D5) and verify with `xmake test-report --tag="[cmd]"`
- [x] 1.2 Update the `-h`/`-hh` help snapshots for the new `--verbose`/`--quiet` descriptions ("echo progress detail to the terminal (stderr)" style) and verify with the help-layout tests

## 2. Echo levels and stream

- [x] 2.1 Add failing logging tests: at level 1, info and warning records echo to stderr in the short format (level name + message; no timestamp, module, `[file:line]`, `[attrs]` — assert the stripping formatter removes the macro-baked location and attribute chains, design D3), error-level records do not echo; at level 2, debug-and-above records echo in the file-log format; the file sink still records the full debug set at both levels; stdout receives no echo lines in either level. Then rebuild the console sink and formatters in `src/logging` (stderr sink, short/full formatters, level filters — design D2/D3/D4) and verify with the logging suite
- [x] 2.2 Add a failing test that a command failure under `-v` prints the clean `error:` line exactly once (same form as without `-v`); remove the verbose special case in `src/app/app_entry.cpp` failure path (design D4) and verify with the app-entry/failure tests
- [x] 2.3 Update the bars-disabled notice to cover both echo levels (wording per design D6) and assert the notice prints once for `-v` and for `-vv`; verify with the video batch tests

## 3. Quiet mode

- [x] 3.1 Add failing tests: with `--quiet` a successful run prints only the final summary line (no scan/progress output), a failing run still prints the `error:` line, the failed-file list, and the log hint, and the log file is written with debug records. Then add the narration quiet gate at the kind-dispatched terminal entry point and the progress-disable path (design D5), and verify with the pipeline/console tests

## 4. End-to-end verification

- [x] 4.1 Run `xmake build e2e_tests && xmake run e2e_tests` (echo-stream separation assertions included), then `xmake test-parallel` to confirm no regressions across unit + e2e suites
