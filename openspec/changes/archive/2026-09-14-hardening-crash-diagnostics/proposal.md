# hardening-crash-diagnostics

## Why

C++26 hardening (`_MSVC_STL_HARDENING=1`) is enabled for Windows builds, but a hardening
violation dies as a bare `ud2` crash (0xC000001D) with no message and no stack; worse, when it
happens inside the unit-test suite, Catch2's `FatalConditionHandler` replaces our crash filter,
so even the existing crash-record machinery never runs — a crashed test leaves only an exit
code. Meanwhile Linux builds (clang + libstdc++, our only CI platform) have no STL hardening at
all, so nothing verifies this class of failure in CI.

## What Changes

- Add a first-chance VEH handler (Windows) in `crash::installHandlers()` that intercepts fatal
  exception codes (AV 0xC0000005, illegal instruction 0xC000001D — the clang-cl hardening trap —,
  stack overflow 0xC00000FD), writes the standard crash record, then continues search so
  third-party filters (Catch2) keep their normal termination flow. VEH cannot be displaced, so
  crash records survive inside Catch2 test runs.
- Attach contextual information to crash records: a pluggable context provider
  (`crash::setCrashContextProvider`), wired in the unit-test runner to report the currently
  running Catch2 test name.
- Emit PDB symbols for Windows release builds (`set_symbols("debug")`) so crash stacks resolve
  to `module!function` + source location instead of raw addresses.
- Enable libstdc++ cheap precondition checks on non-Windows builds
  (`_GLIBCXX_ASSERTIONS=1`): STL violations become `abort()` with a printed reason → SIGABRT →
  existing signal-handler crash record. No ABI change; checks apply to our translation units
  only.
- New crash-child mode `--encro-crash-child=oob` that triggers a real hardening/assertion
  violation (`vector::operator[]` out of range) — exercised by a new integration test on both
  platforms (Windows: VEH path; Linux: assertions → SIGABRT path, runs in CI). An env-gated
  in-suite test case additionally triggers a violation while Catch2's fatal-condition handler
  is engaged — the exact scenario the current filter displacement breaks — and asserts the
  crash record names the running test.
- New suite-level compile-time check that the hardening macros are active on each platform,
  guarding against build-config regressions silently disabling hardening. This is the CI
  verification: the suite runs on the Linux CI matrix, so activation (compile-time) and the
  violation→crash-record chain (runtime) are both verified there. Per decision, no Windows CI
  job in this change — the Windows VEH runtime path is verified locally (documented in tasks).

## Capabilities

### New Capabilities
- `stdlib-hardening`: Standard library hardening activation and verification — which hardening
  macros each platform's builds must define, that violations terminate the process
  unrecoverably, and that the test suite verifies activation (compile-time) and the
  violation-to-crash-record chain (runtime) on every platform the suite runs on.

### Modified Capabilities
- `error-visibility`: Crash reports must also be produced when a fatal hardware/STL exception
  occurs while a third-party top-level filter owns the unhandled-exception path (VEH coverage),
  and crash records should carry available process context (e.g. the running test name).
- `test-failure-diagnostics`: The "unexpected test crash leaves a crash record" requirement is
  tightened: the record must identify the running test when available, and must be produced for
  STL hardening/assertion violations — not only for crashes that reach the unhandled-exception
  filter.

## Impact

- `src/infra/crash_runtime.{h,cpp}` — VEH handler (Windows), context-provider hook, shared by
  `encro` and `tests` binaries (both already call `installHandlers()`).
- `xmake.lua` — non-Windows `add_defines("_GLIBCXX_ASSERTIONS=1")`; release `set_symbols("debug")`
  (Windows release gains PDBs; shipped artifacts unaffected — xpack packages the exe only).
- `tests/test_main.cpp` — context-provider injection + `--encro-crash-child=oob` mode.
- `tests/infra/crash_runtime_tests.cpp` — hardening compile-time test + oob crash integration test.
- CI: no workflow change — new tests ride the existing Linux unit-test step; Windows runtime
  path is locally verified by design decision (no Windows runner in this change).
- No dependency, API, or ABI changes; Linux prebuilt/system libraries are unaffected by
  `_GLIBCXX_ASSERTIONS` (header-inline checks, our TUs only).
