## Context

See `proposal.md` — Why. The state that shapes the approach:

- `appentry::run` (`src/app/app_entry.cpp:275-317`) dispatches: `handleParseAndHelp` (help/version/parse error), `runPreview`, `organize::runOrganizeCommand`, `cmd::runConfigCommand`, `cmd::runCompletionCommand`, then the config/toolchain/pipeline path.
- Teardown copies: `failWithHint` (`:115-132`), `runPreview` (`:233-238`), `runAppPipeline` (`:248-261`); the subcommand exits (`:290-307`) print the hint only. The order in every copy is `printLogHint` → `logRunSummary` → `shutdown`.
- `prelude::initStartup` (`src/app/prelude.cpp`) calls `setupLogging` when `(!cmd.help && !cmd.version) || cmd.error.has_value()` — so help/version with no error write no log file, and everything else does.
- `buildSummary(appctx::AppContext const* ctx, std::string status)` (`app_entry.cpp:43-65`) fills `jobId`/`tasksTotal`/`tasksFailed` only when `ctx != nullptr && ctx->runtime.jobState != nullptr`; `elapsedMs` comes from the file-static `gRunStartedAt` (`:37`).
- `runPreview` builds its own local `AppContext` (`:185`) and never sets `runtime.jobState`; the pipeline path fills the shared `ctx` (`:312-316`).
- Status mapping: `runAppPipeline` (`:255-258`) maps `kCanceledExitCode` → `interrupted`; `runPreview` (`:236`) does not. `logging-behavior:171` requires the summary for every logged run and `:184` the `interrupted` status for canceled runs. Preview never returns `kCanceledExitCode`: a canceled preview fails at `preview_process.cpp:660-661` (`Preview canceled by user.`), which `runPreview` turns into `failRun` → exit `1`.
- Tests: `tests/logging_summary_tests.cpp` (four cases: the counting sink, the summary record's fields, and two echo-level cases); `tests/e2e/encro_e2e_tests.cpp:1777` pins the interrupted status for the pipeline path; `lastNdjsonRecord(logRoot)` (`:1697`) reads the newest ndjson log's last record and is built to run inside `waitUntil` predicates. No test asserts a summary for `organize`/`config`/`completion`.

## Goals / Non-Goals

**Goals:**

- Make "every logged run ends with one summary record" true by construction: one teardown, reached by every path that logged.
- Make the status mapping single and testable, so the `interrupted` class cannot drift between paths.
- Keep the console ordering, the exit codes and the log content exactly as they are, apart from the corrected status of a canceled preview and the help hint moving ahead of the `Log file:` hint on stderr.

**Non-Goals:**

- **No change to cancellation semantics**: exit codes, `stopsignal` handling, and which runs are canceled stay as they are.
- **No hoisting of preview's `AppContext`**: `buildSummary`'s job fields require a store that preview never creates, so sharing `run`'s context would change preview's construction path for no observable difference (D5).
- **No restructuring of `pipeline::run`'s job-state lifecycle** (its own candidate) and no change to `failRun`'s error text or to when the help hint prints.
- **No new logging behavior**: the record's fields, formats and level counts are `logging`'s business and stay untouched.

## Decisions

**D1 — `run` owns the teardown; the paths below it return codes.**

```cpp
// app_entry.h
auto runStatus(int exitCode, bool stopRequested) -> std::string;  // testable, no logging dependency
```

`run` holds `auto ctx = appctx::AppContext{};` (empty until the pipeline path fills it) and never returns from a dispatch branch: every branch — help/version, `handleParseAndHelp`'s error path, the subcommands, preview, the config/toolchain failures, the pipeline — assigns its code to one `exitCode` local, and `run` falls through to the funnel exactly once:

```cpp
if (startup.loggingActive) {
  if (exitCode != 0 && exitCode != stopsignal::kCanceledExitCode) { logging::printLogHint(); }
  // "Interrupted" is the cancel exit code, or any non-zero exit while a stop
  // is pending (preview reports its cancel as an error, exit 1, not 130).
  // A successful run stays "success" even if a stop arrives in the last instant.
  logging::logRunSummary(
    buildSummary(&ctx, runStatus(exitCode, stopsignal::isStopRequested()))
  );
  logging::shutdown();
}
return exitCode;
```

Help/version reach the funnel too, where `loggingActive` is false and it is a no-op — no path has to remember to skip it.

Path shape after the change: `handleParseAndHelp` keeps returning `std::optional<int>` (help/version print and return `0`; a parse error calls `failRun`), and `run` assigns that value to `exitCode` instead of returning it; `runPreview`/`runAppPipeline` return their exit code and print their own failure via `failRun`; the subcommand blocks return the subcommand's code. Nothing below `run` calls `printLogHint`, `logRunSummary` or `shutdown` any more.

*Alternatives:* `finishRun(exitCode, ctx)` called at each of the seven exits keeps the copies' shape and the "remember to call it" hazard that produced the violation; a RAII guard hides `logging::shutdown` in a destructor, which reads worse at `return` points than one explicit funnel, and buys nothing the funnel does not.

**D2 — The funnel is gated on `StartupContext.loggingActive`.**
`initStartup` is the only place that knows whether logging was set up, so it records that fact instead of letting `run` re-derive `(!help && !version) || error`. `loggingActive` is set in the same branch that calls `setupLogging`, so the two cannot disagree.

**D3 — `runStatus(exitCode, stopRequested)` is a pure function in `app_entry.h`.**
`0` → `"success"` (a successful run stays successful even if a stop arrives in the last instant), `kCanceledExitCode` → `"interrupted"`, any other non-zero code → `"interrupted"` when a stop is pending and `"failed"` otherwise. Header-visible so a unit case pins all four classes without spawning a process; no logging or context dependency. Both facts are needed because preview's cancel path surfaces as an error (`preview_process.cpp:660-661` → `failRun` → `1`, never `130`), and the pipeline's `130` returns keep mapping through the exit code alone.
*Alternatives:* the inline lambda in `runAppPipeline` plus a duplicated ternary for preview is today's state and is why the two disagree; making preview return `kCanceledExitCode` would change its exit code, which this change does not do; reporting `interrupted` from the stop fact alone (ignoring the exit code) would flip a run that finished successfully with a late Ctrl-C from `success` to `interrupted` — a behavior change beyond this change's declared scope.
*Alternative:* keeping the inline lambda in `runAppPipeline` and duplicating the ternary for preview — that is today's state, and it is why the two disagree; making preview return `kCanceledExitCode` instead would change its exit code, which this change does not do.

**D4 — `failRun` returns; it does not tear down.**
```cpp
auto failRun(std::string const& message, bool showHelpHint = false) -> int;
```
It keeps the error line on stderr, the `LOG_ERROR` record and the optional help hint, and returns `1`. Callers `return failRun(...)` — the funnel does the rest. The help hint still prints here (console-only) on the same failures; because `failRun` runs before the funnel, it now precedes the `Log file:` hint on stderr instead of following it (in verbose runs it likewise precedes the drain's echoed records) — a cosmetic reorder nothing asserts.

**D5 — `buildSummary` keeps taking a nullable context, and preview passes none in the funnel.**
Preview's local `AppContext` has no `jobState`, so `buildSummary(&previewCtx, …)` and `buildSummary(nullptr, …)` produce the same record; hoisting the context to `run` would change preview's construction for zero difference. The funnel therefore passes `&ctx` (the pipeline path's, empty otherwise), and the record's job fields remain "when job state was active", as the spec states.

**D6 — Tests.**
- **e2e (new)**: `config set crf 23` against a temp `ENCRO_CONFIG` and the test log root, with `--log-json` before the subcommand, ends with a ndjson record carrying `summary` with `status` `success`, found by polling `lastNdjsonRecord` with `waitUntil` — the summary is written during shutdown, so it trails the process's own output. This is the named regression for the violation; `organize --dry-run` would need the extra `ENCRO_FAKE_TAGGER` fixture env, so `config set` is the cheaper vehicle.
- **unit (new)**: `appentry::runStatus` pins all four classes — `(0, false)` and `(0, true)` → `success`; `(kCanceledExitCode, false)` → `interrupted`; `(1, false)` → `failed`; `(1, true)` → `interrupted`. Because the stop fact is a parameter rather than a global read, the canceled-preview case that cannot be staged in the harness is covered here instead of being left unpinned.
- **untouched**: `tests/logging_summary_tests.cpp` (the record's shape), `tests/e2e/encro_e2e_tests.cpp:1777` (the pipeline's interrupted status), and the quiet-mode summary assertions (`:581-672`).
- **not stageable today**: a canceled-preview e2e — the only preview e2e is the `[real-ffmpeg]` smoke (`tests/e2e/encro_e2e_tests.cpp:2481`) and there is no fake-toolchain preview path; task 3.2 records that instead of adding a flaky case.

## Risks / Trade-offs

- [The funnel changes what a failing run prints] → It reproduces the existing teardown order exactly (`printLogHint` → `logRunSummary` → `shutdown`), and `failRun` keeps the error line and help hint on the same failures; the quiet-mode and log-hint assertions in the e2e suite are the gate. The only order shift is the help hint now preceding the `Log file:` hint on stderr.
- [A run that never set logging up would now write a summary] → Gated on `loggingActive` (D2), which is set exactly where `setupLogging` runs.
- [A canceled preview's stop fact is not in the exit code] → `runStatus` takes the stop fact as a parameter (D3), so a non-zero exit under a pending stop reports `interrupted`; the rule is pinned by the unit case, not by a staged canceled preview.
- [A dispatch branch returns before the funnel and skips its summary] → D1 removes direct returns from `run`: every branch assigns `exitCode` and falls through. The only runs that reach the funnel with `loggingActive` false are help/version, which write no log.
- [The new e2e case is flaky against shutdown timing] → It polls with `waitUntil` and tolerates a still-empty log (`lastNdjsonRecord` returns `nullopt` by design), the same pattern the existing summary section uses.
- [The canceled-preview status is a visible change] → Declared in the proposal as the change's one behavior fix, with the spec line that requires it; the exit code itself is unchanged, and a successful run's status is untouched (D3).
- [`ctx` is empty for preview/subcommands, so their summaries lack job fields] → That is what the spec means by "when job state was active", and it matches today's preview summary exactly.

## Migration Plan

None: no persisted format, no CLI surface, no spec-level behavior change beyond the declared status correction. Rollback is a revert of the single commit.
