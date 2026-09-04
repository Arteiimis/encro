## 1. Tests first (RED)

- [ ] 1.1 Add an e2e test: a failed run (invalid option) prints `Log file: <path>` on stderr and the named log file exists on disk
- [ ] 1.2 Add e2e assertions that successful runs and `-h`/`--version` runs print no `Log file:` hint on stdout or stderr (extend the existing completion stdout check with stderr where needed)
- [ ] 1.3 Run the new tests against the current build and confirm they fail for the right reason (hint still printed at startup, absent on failure)

## 2. Implementation (GREEN)

- [ ] 2.1 `src/app/prelude.cpp`: delete the startup hint print in `setupLogging`; drop the now-unused `std::optional<fs::path>` return plumbing if nothing consumes it
- [ ] 2.2 `src/app/app_entry.cpp`: in `failWithHint`, print `Log file: <path>` via `terminal::eprintln(Hint, ...)` when `logging::currentLogFilePath()` has a value, after the error message and before `logging::shutdown()`
- [ ] 2.3 Run `xmake test-report` and the e2e suite; both fully pass; update any test that asserted the old startup hint

## 3. Wrap-up

- [ ] 3.1 Run `xmake fmt` and confirm formatting is clean
- [ ] 3.2 Commit planning artifacts as a `docs:` commit, then implementation + tests + checked tasks in one commit
- [ ] 3.3 Run the `code-review` skill per AGENTS.md and triage/fix findings
