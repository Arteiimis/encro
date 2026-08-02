## 1. CLI Flag Definition Changes (foundation)

- [x] 1.1 Add `advanced` (bool) and `defaultDisplay` (optional string) members to `CmdFlagDef`; mark the 9 advanced flags (`-v`, `--log-json`, `-F`, `--color`, `-I`, `--state-file`, `--force-conflict-handling`, `-x`, `--preset`); wire `default_str` in `registerFlag` for `-q` (=2), `--crf` (=28), `--preset` (=auto), dropping `expectedMin` to 0 for those three if CLI11 rejects the combo (verify with existing cmd tests)
- [x] 1.2 RED: add cmd test asserting `--flat` is rejected as unknown option; then delete the `--flat` def, applyMap entry, and `CmdParseResult.flat` field; update `--keep` description to `(default: flatten)`
- [x] 1.3 RED: add cmd test asserting `-e`/`--verbose-echo` is rejected as unknown option; delete its def, applyMap entry, `CmdParseResult.verboseEcho`, `AppContext.verboseEcho`, and the `config_builder.cpp:328` mapping
- [x] 1.4 Update help-text strings in def arrays: `-h` (`show help; use -hh to show all options`), `--resume` (`require matching previous job state; error if missing or mismatched`), `--force-conflict-handling` (explain `y`/`n` values and default); drop prose `default=10` from `-j`; add `-hh` to usage line 4 (`encro -h | -hh | --version`)
- [x] 1.5 RED: update `encode_config_tests.cpp` "defaults to cq 28" to assert `-cq 28`; remove the five dead member defaults from `EncodeConfig` (outputFormat, videoCodec, crf, nvencPreset, webpQuality — always overridden by the runner's designated initializers; the real defaults live in `buildCMD`'s `value_or` fallbacks, with crf at 28 per the intended default). Keep `ffmpegPath = "ffmpeg"` (buildCMD has no fallback for it)

## 2. Help Tiering

- [x] 2.1 RED: add formatter tests (new `[cmd]` test file) asserting: `-h` output contains no advanced options and ends with `Run 'encro -hh' to view all options.`; `-hh` output contains advanced options and no hint line; `-h -h` equals `-hh`; parse-error path renders the brief tier
- [x] 2.2 Implement tiering in `makeHelpFormatter`: capture help option pointer, read `count() >= 2` at render time, skip advanced options (matched by `get_lnames().front()` per design D2), append the hint line to the brief tier only

## 3. Logging Behavior

- [x] 3.1 RED: add logging tests asserting: a default run (no `-v`, no `--log-json`) creates the timestamped log file and prints `Log file: <path>`; `--log-json` creates both `.log` and `.ndjson`; update existing tests that assert logging is off by default (they now assert it is on)
- [x] 3.2 Rework `LogConfig` (drop `verboseEnabled`/`verboseEchoEnabled`, add `echoEnabled`); in `logging::setup` delete the early `set_level(off)` branch and the file-sink gate so the file sink is always created; console sink only when `echoEnabled`
- [x] 3.3 Rewrite `prelude::setupLogging`: always call `logging::setup`, always print `Log file: <path>` when created; delete the `-e` warning branch; delete `printVerboseLogDirHint` and drop `StartupContext.verboseLogFilePath`
- [x] 3.4 Update `failWithHint` (app_entry.cpp:84): no `-v` → `terminal::println(Error)` + `LOG_ERROR`; `-v` → `LOG_ERROR` only
- [x] 3.5 Change `video_batch_execution.cpp:317` to gate on `ctx.config.verbose` alone; reword warning to `Verbose output enabled: progress bars are disabled.`
- [x] 3.6 RED: add test asserting `-v` disables progress bars / triggers the warning path; adjust existing tests that referenced `-e` or the old `-v` file-only behavior

## 4. Job-State Mismatch Warning

- [x] 4.1 RED: add `[job-state]` tests asserting: mismatched saved state without `--resume` prints a console warning and starts fresh; `--resume` with mismatched state still errors (no discard, no warning); no state file prints no warning
- [x] 4.2 Implement: `Store::initialize` out-param reporting the discarded-mismatch case; `pipeline::ensureJobState` prints `terminal::println(Warning, ...)` and logs the discard

## 5. E2E and Verification

- [x] 5.1 Update e2e tests/fake tooling that pass `-e` or rely on old `-v`/no-log-file behavior
- [x] 5.2 Full verification: `xmake build tests && xmake run tests`, `xmake build e2e_tests && xmake run e2e_tests`, `xmake format -k check`, manual `encro -h` / `encro -hh` output review against the tiering spec
