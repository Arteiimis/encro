## 1. Hardening build flags + compile-time verification (TDD)

- [ ] 1.1 Add compile-time hardening activation test in `tests/infra/` (new `hardening_tests.cpp`, tag `[hardening]`): `STATIC_REQUIRE` that `_MSVC_STL_HARDENING == 1` on Windows and that `_GLIBCXX_ASSERTIONS` is defined on other platforms. Verify: fails on Linux today (define missing), passes on Windows.
- [ ] 1.2 `xmake.lua`: add `add_defines("_GLIBCXX_ASSERTIONS=1")` to the non-Windows block; add `set_symbols("debug")` for Windows release. Verify: 1.1 green on both platforms; Windows release build produces `tests.pdb`; a Linux compile command in `build/compile_commands.json` carries `-D_GLIBCXX_ASSERTIONS=1`.

## 2. Crash context provider

- [ ] 2.1 Test first: extend `tests/infra/crash_runtime_tests.cpp` — install a provider via `crash::setCrashContextProvider` returning a fixed string, call `crash::reportCaughtException`, assert the crash record in the log contains that string; uninstall and assert records are context-free again (no placeholder noise). Verify: new test red (API missing).
- [ ] 2.2 Implement `crash::setCrashContextProvider(std::function<std::string()>)` in `src/infra/crash_runtime.{h,cpp}`; the report path appends the context (try/catch-guarded, silent fall-through on provider failure). Verify: 2.1 green; existing crash tests unchanged.
- [ ] 2.3 In `tests/test_main.cpp`, install the real provider before `Catch::Session{}.run`: a lambda returning `Catch::getResultCapture().getCurrentTestName()`. Verify: suite builds and passes; exercised end-to-end by task 4.4.

## 3. VEH first-chance fatal handler (Windows)

- [ ] 3.1 In `src/infra/crash_runtime.cpp` `installHandlers()` (`_WIN32` branch): `AddVectoredExceptionHandler(1, ...)` for fatal codes {0xC0000005, 0xC000001D, 0xC00000FD} → write the standard crash report, remember the `ExceptionRecord` pointer (atomic), return `EXCEPTION_CONTINUE_SEARCH`; all other codes return immediately; the UE filter skips when the `ExceptionRecord` pointer matches the one already reported. Verify: full unit suite green — existing `--encro-crash-child` test (custom non-fatal code) still produces its single UE-filter report, non-crash tests unaffected.

## 4. Out-of-bounds crash-child mode + integration test (TDD)

- [ ] 4.1 Test first: new integration test in `crash_runtime_tests.cpp` spawning self with `--encro-crash-child=oob`; assert non-zero exit, output contains `[CRASH]` and `stacktrace`; on Windows additionally assert the reason mentions `0xC000001D` and that exactly one `[CRASH]` block appears (VEH report + UE-filter suppression). Verify: red (unknown-arg falls through to Catch2, non-zero exit).
- [ ] 4.2 `tests/test_main.cpp`: handle `--encro-crash-child=oob` — install handlers, install a context provider returning a fixed test name, trigger `vector::operator[]` out-of-range. Verify: 4.1 green on Windows (VEH report incl. context) and on Linux (assertion → SIGABRT → signal-handler report).
- [ ] 4.3 Extend 4.1's assertion: output contains the context provider's fixed name (verifies D3 plumbing through the crash path). Verify: green on both platforms.
- [ ] 4.4 In-session crash verification: add an env-gated Catch2 test case (no-op unless `ENCRO_TEST_CRASH_OOB=1`, otherwise out-of-bounds access) plus an integration test that spawns `tests` with that env var and a test-spec filter naming the case; assert non-zero exit, output contains `[CRASH]` and the crashing test's name (real provider from 2.3, while Catch2's `FatalConditionHandler` owns the filter slot). Verify: green on Windows (VEH under active session) and Linux (assertion → SIGABRT under active session).

## 5. Verification

- [ ] 5.1 Local Windows verification: `xmake test-report` green; run `tests.exe --encro-crash-child=oob` manually and confirm the single crash record shows resolved symbols (`tests!...` from the release PDB) plus the exception code. Delivered: command transcript noted in the change.
- [ ] 5.2 CI verification (Linux matrix): push the branch and confirm the unit-test step passes with the new `[hardening]` compile-time test and the oob integration test running on Linux (assertions → crash record). This is the CI verification required by the proposal; no ci.yml change expected.
