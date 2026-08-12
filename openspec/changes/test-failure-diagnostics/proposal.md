# test-failure-diagnostics

## Why

When a test fails, the information needed to start debugging does not reliably reach the developer or agent. A passing `xmake run tests` already produces 5259 lines of output, of which 4997 (95%) are indicators progress-bar frames; a failure block produced in the middle of a run is buried in that noise and often lost entirely to output truncation (only the last 2000 lines are retained by tooling). The end-of-run summary prints only failure *counts*, never the names of failed tests. E2E failures frequently show only `1 == 0` because the child process's stdout/stderr are not captured at 16 assertion sites. On failure, the most valuable evidence — job-state files, fake-tool invocation logs, partial outputs — is deleted by `TempDir`'s destructor. A crash in any test yields a bare exit code with no stack. Linux CI already works around the reporting gap with JUnit+grep ("console output swallowed by progress bars"); local Windows runs have no equivalent.

## What Changes

- **JUnit report + failure summary for local runs**: a new xmake task (plugin, modeled on `plugins/coverage`) runs the unit tests with a JUnit reporter writing a report file (e.g. `build/last-test-report.xml`) alongside the console reporter, and on non-zero exit prints a compact failure summary (test name, file:line, message) so the first read of the output already locates every failure. Exit code propagation is preserved.
- **Progress bars render only on a terminal**: bar frames are suppressed when stdout is not a TTY (`indicators::option::Stream` redirected to a null stream), removing the 95% noise share from test runs. Side effect (**BREAKING** for piped output only): piping `encro` itself no longer emits bar frames; text lines (e.g. `All pictures packed successfully`) are unaffected. No test asserts on bar-frame text (verified), and e2e children (pipes) therefore stop emitting frames too.
- **E2E failure context**: a `REQUIRE_SUCCESS(result)` macro (Catch2 custom-macro pattern) that automatically dumps the child's stdout/stderr as `INFO` and asserts exit code 0 at the call site (file:line preserved). The 16 bare `REQUIRE(result.exitCode == 0)` sites adopt it; existing `CAPTURE` + `REQUIRE` pairs are left as-is.
- **Evidence retention on failure**: `TempDir` keeps its directory when the test is failing (`std::uncaught_exceptions() > 0` covers `REQUIRE`, which throws) and prints the kept path to stderr so the failure output points at the surviving state files, fake-tool invocation logs, and partial outputs.
- **Crash diagnostics in tests**: the test binary installs the crash handler in its main process (not only in `--encro-crash-child` mode), so an unexpected segfault produces a crash record instead of a bare `0xC0000005`.

## Capabilities

### New Capabilities

- `test-failure-diagnostics`: contract that a failing test run leaves the developer/agent with everything needed to locate and start debugging a failure — a persistent machine-readable report, a failure summary naming each failed test, a test console unpolluted by progress frames, e2e failures showing the child's output, evidence files preserved on failure, and crash records on abnormal termination.

### Modified Capabilities

(none — existing spec-level behavior of `progress-scroll-label` and the app is unchanged; bar-frame suppression on non-TTY stdout is a rendering gate, not a scroll/display contract change)

## Impact

- **Code**: `tests/test_utils.h` (TempDir retention), `tests/test_main.cpp` (crash handler), `tests/e2e/encro_e2e_tests.cpp` (macro adoption), `src/core/progress.cpp` (non-TTY stream gate), new plugin task file under `plugins/` (report + summary wrapper).
- **API**: internal test-only additions (`REQUIRE_SUCCESS` macro); no product API changes.
- **Behavior**: piped (non-TTY) `encro` output no longer contains progress-bar frames; all console text lines unchanged. `xmake run tests` behavior unchanged; the new task is additive.
- **CI**: Linux CI already produces JUnit and greps it — unchanged; local workflow gains parity.
- **Tests**: e2e tests re-verified to not depend on bar-frame text in captured child output; new tests cover TempDir retention and the failure summary output.
