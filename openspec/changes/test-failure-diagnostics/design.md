# test-failure-diagnostics — Design

## Context

- Test stack: Catch2 v3.15.2, custom runner `tests/test_main.cpp` (unit) and `CATCH_CONFIG_MAIN` (e2e); targets `tests` / `e2e_tests`; shared fixtures in `tests/test_utils.h` (`TempDir`, `StdoutCapture`, ...); e2e process helpers in `tests/e2e/e2e_test_utils.h`.
- Runner: `xmake run tests` executes the built binary directly; xmake propagates the exit code (Catch2 returns 42 on test failures, 2 when no tests ran).
- Measured noise: a passing run prints 5259 lines, 4997 of them indicators progress-bar frames (95%); spdlog console lines and app `std::cout` text make up the rest.
- Existing assets to reuse: `plugins/coverage/xmake.lua` (plugin task pattern, binary-path resolution, execv wrappers), `src/infra/terminal.cpp` (isatty-based detection, currently color-only and file-local), `src/infra/crash_runtime.*` (3-tier crash reporting ending in stderr), Linux CI already produces JUnit via `-r junit -o /tmp/ut.xml` and greps failures.
- Verified facts this design relies on: indicators v2.3 supports `option::Stream`; no unit/e2e test asserts on bar-frame text (prompt strings are passed to packers but never grepped from output); e2e includes `tests/test_utils.h` so `TempDir` changes apply to both suites; `crash::installHandlers()`'s fallback chain ends in stderr when logging is not initialized.

## Goals / Non-Goals

**Goals:**
- Every `xmake`-invoked unit-test run leaves a machine-readable report; failed runs end their output with a failure summary that names each failed test.
- Reduce unit-test console output from ~5k lines to roughly the Catch2 dot/summary volume when stdout is not a terminal.
- Failed e2e runs show child stdout/stderr and failed runs keep their scratch directories.

**Non-Goals:**
- No change to `xmake run tests` behavior itself (ad-hoc runs stay untouched; the new task is additive).
- No per-assertion `INFO`/`CAPTURE` retrofitting in unit tests (only mechanical e2e macro adoption).
- No app-level spec changes; bar-frame suppression is a rendering gate, and the product's piped output changing is accepted as a deliberate side effect.
- No structured error codes or report aggregation across multiple runs (last run wins).

## Decisions

### D1: Report + summary as a new xmake plugin task (`test-run` style, not modifying `xmake run tests`)

New plugin `plugins/test_report/xmake.lua` exposing a task (e.g. `xmake test-report`), modeled on the coverage plugin: resolve the binary at `build/<platform>/<arch>/<mode>/tests(.exe)`, run `tests.exe -r console -r junit -o build/last-test-report.xml`, propagate the exit code, and on non-zero exit parse the XML with plain lua patterns (no new dependency — CI already greps it with `grep -oE`) and print `FAILED: <testcase name> at <file>:<line> — <message>` lines plus the report path.

- `-o` binds to the most recent `-r` (junit), leaving console on stdout — same mechanism CI uses.
- Report path `build/last-test-report.xml` is mode-independent and stable for agents to grep after a run.
- The task rebuilds the `tests` target first (execv of xmake, same pattern the coverage plugin uses) so one command covers build+run+report.

Alternatives considered: (a) shell script — platform duplication, CI already has the pattern; (b) intercepting `xmake run tests` — xmake tasks don't wrap `run` cleanly and it would change existing muscle memory; (c) compact reporter (`-r compact`, one line per failure) — nice for grep but junit is structured and CI-proven.

### D2: Non-TTY bar gate via a null-stream `option::Stream` in `progress.cpp`

At bar construction (`makeBar` / `ProgressContext::addBar`), when stdout is not a terminal, pass `indicators::option::Stream{nullStream}` where `nullStream` is a file-local `std::ostream` over a minimal null `streambuf` (~10 lines). Terminal detection: export a small `terminal::streamIsTerminal(Stream)` wrapper from `src/infra/terminal.cpp` (the isatty logic already exists there, file-local, used by `colorsEnabled`). Frame suppression therefore applies everywhere bars are used — unit tests, e2e children (pipes), and piped `encro` runs alike. Text lines via `terminal::write` are untouched.

Alternatives considered: (a) a `--quiet`/verbose knob threaded through pipeline config — plumbing through every call site for a property that can be auto-detected; (b) suppressing only in tests — would leave e2e `stdoutText` full of frames and product pipes noisy; (c) disabling bars entirely under non-TTY — loses the product's piped progress reporting... which is exactly the point: bar frames are terminal-only UI, text status lines already carry the information.

### D3: `REQUIRE_SUCCESS` macro in `tests/e2e/e2e_test_utils.h`

```cpp
#define REQUIRE_SUCCESS(result)                                               \
  do {                                                                        \
    INFO("child stdout:\n" << (result).stdoutText << "\nchild stderr:\n"      \
                           << (result).stderrText);                           \
    REQUIRE((result).exitCode == 0);                                          \
  } while (false)
```

`INFO` before `REQUIRE` means Catch2 prints the child output only when the assertion fails; macro expansion keeps `__FILE__`/`__LINE__` at the call site (the "identify the assertion's source location, not a shared helper" requirement). The 16 bare `REQUIRE(result.exitCode == 0)` sites adopt it; existing `CAPTURE`+`REQUIRE` pairs stay as-is.

Alternative considered: a checker *function* — loses the call site (reports e2e_test_utils.cpp), which is worse for locating the failing test.

### D4: `TempDir` retention on failure via `std::uncaught_exceptions()`

In the `TempDir` destructor: if `std::uncaught_exceptions() > 0`, print `kept temp dir on failure: <path>` to `std::cerr` and skip `remove_all`; otherwise clean up as today. Catch2 v3 `REQUIRE` throws `TestFailureException`, so the destructor runs mid-unwind and the counter is > 0 — passing tests and `CHECK`-only failures unwind nothing and still clean up. `std::cerr` (not `INFO`) because the FAILED block has already been emitted by the time the destructor runs. Both unit and e2e suites get it from the shared header. An uncaught non-Catch2 exception also retains — desired.

Alternatives considered: (a) Catch2 event listener (`testCaseEnded` sets a "last test failed" flag) — covers CHECK-only failures but needs a listener + global registry (~20 lines) for a rare case; deferred with a note, (b) keeping a fixed number of recent temp dirs — accumulates junk on every passing run.

### D5: Crash handler in the main test process

`tests/test_main.cpp` calls `crash::installHandlers()` at startup (before the Catch session). The handler is idempotent (`compare_exchange` guard), so the `--encro-crash-child` mode is unaffected, and crash-runtime tests that spawn children still exercise their own in-process handlers. In the test process logging is never initialized, so the direct-file tier and async tier no-op and the stderr tier is the crash record (stack + exception code) — sufficient per spec, and `[real-ffmpeg]`-style diagnostics remain unaffected.

Alternative considered: initializing full logging setup in tests — imports the app's log-file management into test runs for no gain, since the stderr tier already delivers the record.

## Risks / Trade-offs

- [Piped `encro` output loses bar frames; a consumer could depend on them] → Mitigation: verified no test and no documented interface does; frames are terminal UI, text lines carry status; proposal marks it **BREAKING** for piped output.
- [JUnit parsing via lua patterns is fragile if Catch2 changes its XML shape] → Mitigation: CI already greps the same shape; the task prints the report path so the XML itself is always available for manual inspection.
- [`std::uncaught_exceptions` misses CHECK-only failures] → Mitigation: REQUIRE dominates and CHECK-failures still leave the app's log files; listener-based coverage is a noted follow-up if it ever matters.
- [Kept temp dirs accumulate in %TEMP% on repeated failures] → Mitigation: only failures accumulate (passing runs clean up), names are timestamped and greppable; no automatic GC to avoid deleting evidence.
- [Crash handler in tests could interfere with tests that assert on crash behavior] → Mitigation: crash-runtime tests use child processes; handler install is idempotent.
- [Report task rebuild cost when running via the task] → Mitigation: incremental build is near-no-op after `xmake build tests`; ad-hoc `xmake run tests` remains available.

## Migration Plan

- Additive only: new task, macro, and guards. No existing workflow changes. CI unchanged (already produces JUnit on Linux).
- Rollback: delete the plugin and revert the two guards; no persisted state beyond `build/last-test-report.xml`.

## Open Questions

(none — decisions above resolve the spec-relevant ambiguity)
