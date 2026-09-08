## 1. Crash-path zone gate (src/infra)

- [x] 1.1 Add the DLL-load zone to `crash_runtime` (nestable `thread_local`
      depth + `ScopedDllLoadZone` RAII + lock-free query), with unit tests for
      set/clear/nesting semantics plus a cross-thread check (a background
      thread holding the zone leaves the current thread's state unaffected) —
      verify with `xmake test-report --tag="[crash]"`
- [x] 1.2 Gate `writeCrashReport`: skip `captureStacktrace` while zoned and
      note the omission in the message; unit test asserts a zoned
      `reportCaughtException` record carries the marker and no stack frames
      (existing logger-capture harness). The terminate and
      unhandled-exception-filter entries funnel through the same
      `writeCrashReport`, so this gate is the structural proxy covering the
      "Unhandled exception on a DLL-load-zone thread" scenario.
- [x] 1.3 Gate `vehFatalHandler`: return `EXCEPTION_CONTINUE_SEARCH` silently
      while zoned; in-process test raises `EXCEPTION_ACCESS_VIOLATION` via
      `RaiseException` inside `__try/__except` — under the zone the local
      handler catches it and no record is written, without the zone a record
      appears (control)

## 2. DLL-load site guards (src/tagger)

- [x] 2.1 Wrap the `LoadLibraryExW` loop in `ensureGpuRuntimePaths`
      (onnx_tagger.cpp) with `ScopedDllLoadZone`; verify the unit suite stays
      green (`xmake test-report`)
- [x] 2.2 Wrap the `LoadLibraryW`/`FreeLibrary` window in `hasNvidiaDriver`
      (model_store.cpp) with `ScopedDllLoadZone` plus the `ponytail:` ceiling
      comment (DLL initialization inside third-party library calls remains the
      documented ceiling); verify with `xmake test-report --tag="[tagger]"`

## 3. No-hang regression (integration)

- [x] 3.1 Add the `--encro-crash-child=dll-zone` mode to `tests/test_main.cpp`
      (install handlers, enter the zone, raise the fatal-code exception inside
      `__try/__except`, exit 0 promptly); verify the mode runs standalone
- [x] 3.2 Add the parent integration test spawning that mode with a timed
      wait (kill on expiry, e.g. via a bounded `wait_for` instead of the
      blocking `child.wait()` in `spawnSelf`); assert the child's pipes reach
      EOF and it exits 0 well inside the bound — the pre-fix code hangs at the
      raise, so a regression must fail fast rather than hang the suite
- [x] 3.3 Manual verification on this machine (model files present):
      `tests.exe "[real-model]"` completes instead of hanging at the seed
      line; record the outcome in the change notes (CI lacks the model and
      SKIPs, as today)

## 4. Verification and wrap-up

- [x] 4.1 Run `xmake fmt -k` clean and `xmake tidy` over the touched files;
      then full `xmake test-report`, the e2e suite, and several
      `xmake test-parallel` runs including the previously-hanging
      first-test condition
- [x] 4.2 Mark the backlog entry "Flaky `test-parallel` hang" resolved with
      the fix commit hash once landed
