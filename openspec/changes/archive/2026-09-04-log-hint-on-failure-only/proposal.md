## Why

The startup hint `Log file: <path>` currently prints on every non-help run (transcodes, `preview`, `completion`, `config`, even typo'd commands), which users experience as noise: most runs succeed and never need the log path. The hint's actual value is pointing at the log when something went wrong, so it should appear exactly then.

## What Changes

- Remove the unconditional startup hint `Log file: <path>` from `prelude::setupLogging`.
- Print `Log file: <path>` on failed runs only, from `failWithHint` (the shared failure exit in `app_entry.cpp`), after the error message, on stderr, whenever a log file exists for the run.
- Logging behavior otherwise unchanged: the log file is still written on every run (including `-h` with a parse error), still skipped for pure help/version runs.

## Capabilities

### New Capabilities

- (none)

### Modified Capabilities

- `logging-behavior`: the always-on-file-logging requirement changes from "SHALL print the hint at startup" to "failed runs SHALL print the hint with the error; successful runs print no hint". The log-file-is-written-every-run behavior itself is unchanged.

## Impact

- `src/app/prelude.cpp` (delete hint print in `setupLogging`), `src/app/app_entry.cpp` (add hint print in `failWithHint`).
- Tests: unit/e2e tests asserting the startup hint must be updated; a failure-path assertion (hint present after an error) and a success-path assertion (no hint) are added; the e2e `REQUIRE_SUCCESS` log-tail helper keeps working because the hint stays on stderr.
