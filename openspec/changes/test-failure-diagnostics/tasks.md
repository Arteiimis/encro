# test-failure-diagnostics — Tasks

## 1. TempDir failure retention (D4)

- [x] 1.1 RED: add tests in `tests/test_utils_tests.cpp`-style unit test — `TempDir` keeps its directory when destroyed during stack unwinding (throw + catch around a scoped `TempDir`) and removes it on normal scope exit; assert the stderr message contains the kept path (needs a small stderr-capture helper alongside the existing `StdoutCapture`)
- [x] 1.2 Implement the destructor guard in `tests/test_utils.h`: `std::uncaught_exceptions() > 0` → print `kept temp dir on failure: <path>` to `std::cerr` and skip `remove_all`
- [x] 1.3 Run full unit suite — no behavior change on passing runs

## 2. Non-TTY progress bar gate (D2)

- [x] 2.1 RED: add test — with stdout redirected to a file (non-TTY), ticking a `progress::ProgressContext` bar emits no frames into the capture while `terminal::write` text lines still appear
- [x] 2.2 Export `terminal::streamIsTerminal(Stream)` from `src/infra/terminal.*` (reuse the existing file-local isatty logic)
- [x] 2.3 Implement the gate in `src/core/progress.cpp`: bar construction passes `indicators::option::Stream{nullStream}` (file-local null `streambuf`) when stdout is not a terminal
- [x] 2.4 Run unit + e2e suites and confirm e2e child `stdoutText` no longer contains bar frames; all suites pass

## 3. E2E REQUIRE_SUCCESS macro (D3)

- [x] 3.1 Add `REQUIRE_SUCCESS(result)` macro to `tests/e2e/e2e_test_utils.h` (`INFO` of child stdout/stderr, then `REQUIRE(exitCode == 0)`, do/while form)
- [x] 3.2 Adopt the macro at the 16 bare `REQUIRE(result.exitCode == 0)` sites in `tests/e2e/encro_e2e_tests.cpp` (leave existing `CAPTURE`+`REQUIRE` pairs as-is)
- [x] 3.3 Build + run e2e suite (happy-path coverage from all adopting tests)
- [x] 3.4 Manually verify the failure path: force a child failure (e.g. `ENCRO_FAKE_FFMPEG_EXIT_CODE` on a run), confirm the failure output includes child stdout/stderr and the call-site file:line, then revert the forcing change

## 4. Crash handler in the main test process (D5)

- [x] 4.1 RED: add test — `crash::installHandlers()` is callable in-process and idempotent, and `reportCaughtException` writes the crash record to stderr when logging is not initialized (stderr-capture helper from 1.1)
- [x] 4.2 Call `crash::installHandlers()` at startup in `tests/test_main.cpp` (before the Catch session; `--encro-crash-child` mode unchanged)
- [x] 4.3 Run the `[crash]`-tagged tests and the full unit suite to confirm no interference

## 5. Report + failure-summary task (D1)

- [x] 5.1 Add `plugins/test_report/xmake.lua` task (modeled on `plugins/coverage`): resolve `tests(.exe)` under `build/<platform>/<arch>/<mode>/`, rebuild the `tests` target, run `-r console -r junit -o build/last-test-report.xml`, propagate the exit code
- [x] 5.2 On non-zero exit, parse the XML with lua patterns and print `FAILED: <name> at <file>:<line> — <message>` lines plus the report path
- [x] 5.3 Verify passing path: `xmake test-report` exits 0, report exists with no failure entries, console output ends with the Catch2 summary
- [x] 5.4 Verify failing path with a temporary failing test: summary names the failed test with file:line, exit code non-zero; remove the temporary test
- [x] 5.5 Update `CLAUDE.md` build/run section to document the new task
- [x] 5.6 Full verification: `xmake build tests && xmake run tests`, `xmake build e2e_tests && xmake run e2e_tests`, `xmake test-report`
