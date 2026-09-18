## Why

The end-of-run teardown is hand-copied on three paths (`failWithHint` `src/app/app_entry.cpp:115-132`, `runPreview` `:233-238`, `runAppPipeline` `:248-261`) and half-copied on the three subcommand exits (`:290-307`), so `organize`, `config` and `completion` runs write a log file with no summary record — violating `openspec/specs/logging-behavior/spec.md:171` — and the copies have drifted, so a canceled `preview` reports `failed` where the interrupted scenario (`:182-184`) requires `interrupted`. The exit paths are the last place where "remember to do the four steps" is the only thing keeping the spec true, and every new subcommand adds another chance to forget.

## What Changes

- **One teardown, one place.** `appentry::run` owns the sequence — `printLogHint` on failure, `logRunSummary(...)`, `shutdown()` — and no dispatch branch returns from it directly: every path below it assigns its exit code and falls through to the funnel exactly once. `failWithHint` becomes `failRun(message, showHelpHint)`: it prints the error line, logs it, optionally prints the help hint, and **returns** the code instead of tearing down.
- **The funnel is gated by the fact, not by another copy of the condition.** `prelude::StartupContext` gains `loggingActive`, set where logging is set up (`prelude.cpp`, whose condition is `(!cmd.help && !cmd.version) || cmd.error.has_value()`), so help/version runs — which write no log file — skip the summary without anyone re-deriving that rule.
- **The status mapping becomes one testable function, fed both facts.** `appentry::runStatus(int exitCode, bool stopRequested)`: `0` → `success` (a successful run stays successful even if a stop arrives in the last instant), `kCanceledExitCode` → `interrupted`, any other non-zero code → `interrupted` when a stop is pending, `failed` otherwise. Preview's cancel path surfaces as an error (exit `1`, never `130`), so the stop fact is what distinguishes it.
- **Subcommands gain the summary and the drain** by going through the funnel: `organize`, `config` and `completion` runs now end with a summary record, as `logging-behavior:171` requires.
- **Tests**: an e2e case that a subcommand run ends with a summary record (`config set` under a temp `ENCRO_CONFIG` and log root; `lastNdjsonRecord` at `tests/e2e/encro_e2e_tests.cpp:1697`, polled with `waitUntil` because the summary is written during shutdown), and a unit case for the four status classes. Existing coverage stays: `tests/logging_summary_tests.cpp` owns the record's formatting, and the interrupted-status e2e section (`tests/e2e/encro_e2e_tests.cpp:1777`) pins the pipeline path.

**The one behavior fix:** a canceled `preview` run now reports `interrupted` instead of `failed`. Today preview's cancel path returns an error (`failRun` → exit `1`), so the exit code alone cannot distinguish it; the funnel passes the stop request into `runStatus` and gets `interrupted`, the same status the pipeline path already reports for its `130`. Exit codes, cancellation handling, and a successful run's status all stay as they are (a run that finishes with a stop arriving in the last instant still reports `success`). The only other observable difference is the console order of advisory stderr lines: the help hint now prints before the `Log file:` hint (and, in verbose runs, before the drain's echoed records).

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None — `logging-behavior` already requires exactly this (the summary for every logged run, and `interrupted` for canceled runs); the change makes the implementation match it rather than altering it, so there is no delta to write. `skip_specs: true` is set in `.openspec.yaml` per the precedent of `refactor-long-param-lists` / `remove-immer-simplify-locks` / `reduce-over-engineering`.

## Impact

- `src/app/app_entry.{h,cpp}` — `run` becomes the single teardown; `failRun` replaces `failWithHint`; `runPreview`/`runAppPipeline` lose their copies; `appentry::runStatus` added
- `src/app/prelude.{h,cpp}` — `StartupContext.loggingActive`, set by `initStartup`
- `tests/e2e/encro_e2e_tests.cpp` — one new case for a subcommand run's summary; `tests/app/app_entry_tests.cpp` — the status-mapping case
- No new files (the status helper lives in `app_entry.h`, where `helpIntroLine` already is), no new dependencies, no CLI or file-format change.

**Explicitly out of scope:** the job-state lifecycle inside `pipeline::run` (its own candidate), preview's own `AppContext` construction (the summary gains nothing from hoisting it — see design D5), and cancellation *semantics* (exit codes and stop handling are unchanged; only the reported status of a canceled preview is corrected).
