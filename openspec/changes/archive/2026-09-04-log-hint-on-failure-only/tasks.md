## 1. Tests first (RED)

- [x] 1.1 Add an e2e test: a failed run (invalid option) prints `Log file: <path>` on stderr and the named log file exists on disk
- [x] 1.2 Add e2e assertions that successful runs and `-h`/`--version` runs print no `Log file:` hint on stdout or stderr (extend the existing completion stdout check with stderr where needed)
- [x] 1.3 Run the new tests against the current build and confirm they fail for the right reason (hint still printed at startup, absent on failure)
- [x] 1.4 Add e2e tests: failed `config`/`completion` subcommand runs and failed pipeline runs print the hint on stderr (code-review follow-up: those failure exits bypass `failWithHint`)

## 2. Implementation (GREEN)

- [x] 2.1 `src/app/prelude.cpp`: delete the startup hint print in `setupLogging`; drop the now-unused `std::optional<fs::path>` return plumbing if nothing consumes it
- [x] 2.2 `src/app/app_entry.cpp`: in `failWithHint`, print `Log file: <path>` via `terminal::eprintln(Hint, ...)` when `logging::currentLogFilePath()` has a value, after the error message and before `logging::shutdown()`
- [x] 2.3 Run `xmake test-report` and the e2e suite; both fully pass; update any test that asserted the old startup hint
- [x] 2.4 Extract `logging::printLogHint()` and call it at every failure exit — `failWithHint`, `runPreview`/`runAppPipeline` non-zero (interrupted stays hint-free), and the `config`/`completion` wrappers in `appentry::run` (code-review follow-up); also fix the CRLF path bug in the e2e `encroLogTail` helper surfaced by task 1.1

## 3. Wrap-up

- [x] 3.1 Run `xmake fmt` and confirm formatting is clean
- [x] 3.2 Commit planning artifacts as a `docs:` commit, then implementation + tests + checked tasks in one commit
- [x] 3.3 Run the `code-review` skill per AGENTS.md and triage/fix findings
