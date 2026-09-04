## Context

`prelude::setupLogging` (src/app/prelude.cpp:26-29) prints `Log file: <path>` to stderr whenever `logging::setup` created a log file, which is every non-help/version run (prelude.cpp:53). `logging::currentLogFilePath()` exposes the path; `logging::shutdown()` clears it. All early failures converge on `failWithHint` (src/app/app_entry.cpp:114), which prints the error, writes the end-of-run summary, then calls `logging::shutdown()`.

## Goals / Non-Goals

**Goals:**
- Successful and help/version runs stop printing the `Log file:` hint entirely.
- Failed runs print the hint once at their failure exit, so the error and its log path appear together.

**Non-Goals:**
- Changing what gets logged or when the log file is created (always-on logging stays).
- Interrupted runs (Ctrl-C): they exit as `interrupted`, not `failed`, and print no hint.

## Decisions

- **D1: Shared `logging::printLogHint()` called at every failure exit.** It prints the hint when a log file exists (no-op otherwise, including after `logging::shutdown()` cleared the path). Call sites: `failWithHint` (parse errors, config-build errors, toolchain failures, preview/pipeline hard errors), `runPreview`/`runAppPipeline` non-zero exits (guarded so the interrupted exit code stays hint-free), and the `config`/`completion` wrappers in `appentry::run` — their bodies report their own errors and return non-zero without reaching `failWithHint`. Alternative (a single call inside `failWithHint` only) leaves subcommand and pipeline-level failures hint-free; that gap was found in review and is covered by e2e tests.
- **D2: stderr via `terminal::eprintln(Hint, ...)`** — the same stream as the old startup hint. stdout carries product output (`encro completion bash` scripts), and the e2e `REQUIRE_SUCCESS` helper scans stderr for `Log file: ` to dump the log tail, so switching streams would break that diagnostic path.
- **D3: Print whenever a log file exists, including `-v` echo runs.** Under `-v` the log lines already echo to console, but naming the file still helps; skipping the hint under `-v` would add a conditional with no observable benefit.

## Risks / Trade-offs

- [`printLogHint` no-ops after `logging::shutdown()`] → Every call site orders it before shutdown; the e2e assertions read the file after process exit, so flushes are complete.
- [Output streams split: `Error:` on stdout, `Log file:` hint on stderr] → Matches existing behavior (the startup hint was stderr for the same reason); terminals show both, redirected stdout stays clean.
