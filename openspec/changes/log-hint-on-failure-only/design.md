## Context

`prelude::setupLogging` (src/app/prelude.cpp:26-29) prints `Log file: <path>` to stderr whenever `logging::setup` created a log file, which is every non-help/version run (prelude.cpp:53). `logging::currentLogFilePath()` exposes the path; `logging::shutdown()` clears it. All early failures converge on `failWithHint` (src/app/app_entry.cpp:114), which prints the error, writes the end-of-run summary, then calls `logging::shutdown()`.

## Goals / Non-Goals

**Goals:**
- Successful and help/version runs stop printing the `Log file:` hint entirely.
- Failed runs print the hint once, at the failure exit, so the error and its log path appear together.

**Non-Goals:**
- Changing what gets logged or when the log file is created (always-on logging stays).
- Surfaces other than `failWithHint` (e.g. partially-failed pipeline runs that still exit through the normal summary path) — they keep their existing failure summary output and stay hint-free.

## Decisions

- **D1: Single print point in `failWithHint`, before `logging::shutdown()`.** `currentLogFilePath()` returns nullopt after shutdown, and `failWithHint` is already the shared exit for parse errors, config errors, and toolchain failures — one call site covers all of them. Alternative (printing at each failure site) duplicates the line across callers for nothing.
- **D2: stderr via `terminal::eprintln(Hint, ...)`** — the same stream as the old startup hint. stdout carries product output (`encro completion bash` scripts), and the e2e `REQUIRE_SUCCESS` helper scans stderr for `Log file: ` to dump the log tail, so switching streams would break that diagnostic path.
- **D3: Print whenever a log file exists, including `-v` echo runs.** Under `-v` the log lines already echo to console, but naming the file still helps; skipping the hint under `-v` would add a conditional with no observable benefit.

## Risks / Trade-offs

- [Runs that fail inside the pipeline (task failures) exit via the normal summary path, not `failWithHint`, so they print no hint] → Accepted: those runs print their own failed-task summary; the log file is still written and `--log-json` consumers already get the `log` path in the summary record. Revisit only if users report needing the pointer there.
- [Output streams split: `Error:` on stdout, `Log file:` hint on stderr] → Matches existing behavior (the startup hint was stderr for the same reason); terminals show both, redirected stdout stays clean.
