## Why

The first-chance VEH crash reporter (hardening-crash-diagnostics) deadlocks the
process when a fatal-code exception is raised during third-party DLL
initialization on the loader-lock-holding thread: the MSVC STL
`std::stacktrace` symbolizer lazily attaches dbgeng to the own process
(`AttachProcess(self)` + `WaitForEvent(INFINITE)`), and that wait can never
complete under loader lock. Confirmed live on 2026-09-08 (backlog: "Flaky
`test-parallel` hang"): `tests.exe "[real-model]"` hangs forever after the
Catch2 seed line when the CUDA provider preload's DLL static init raises a
handled first-chance access violation. `encro.exe` runs the same preload, so
the product carries the same latent hang, not just CI.

## What Changes

- Add a thread-scoped RAII "DLL-load zone" that marks threads which may hold
  the loader lock (the windows around our own `LoadLibrary*` calls).
- Inside the zone, `vehFatalHandler` passes fatal-code exceptions through with
  no report and no stack capture: these are typically handled third-party
  probing exceptions (writing a `[CRASH]` record for them is a false alarm);
  genuinely unhandled ones still reach the unhandled-exception filter /
  terminate paths and are reported there.
- Every crash report path that still writes while on a zoned thread
  (terminate handler, unhandled-exception filter, `reportCaughtException`)
  skips the dbgeng-backed stack capture — reason and context are still
  reported — so no report path can block under loader lock.
- Wrap both first-party DLL-load windows with the guard: the
  `LoadLibraryExW` preload of `onnxruntime_providers_shared.dll` /
  `onnxruntime_providers_cuda.dll` in `ensureGpuRuntimePaths`, and the
  `nvcuda.dll` driver probe (`LoadLibraryW`/`FreeLibrary`) in
  `hasNvidiaDriver` reached from the `--download-models` flow.
- Tests: in-process unit tests for the zone's set/clear semantics and the VEH
  pass-through; a new crash-child mode proving a fatal-code raise on a zoned
  thread exits promptly with no crash record (bounded-time integration test).

## Capabilities

### New Capabilities

_(none)_

### Modified Capabilities

- `error-visibility`: crash interception gains a loader-lock carve-out —
  fatal-code exceptions raised on a DLL-load-zone thread pass through first
  chance without a record, and any crash record written on such a thread omits
  the stacktrace instead of risking a deadlock. This refines the pending
  hardening-crash-diagnostics requirement "Fatal exceptions are reported under
  third-party crash filters" (that change is complete but not yet archived; on
  archive the two requirements read as general rule plus carve-out).

## Impact

- Code: `src/infra/crash_runtime.{h,cpp}` (zone guard, gates in
  `vehFatalHandler` / `writeCrashReport`), `src/tagger/onnx_tagger.cpp` and
  `src/tagger/model_store.cpp` (guards around both DLL-load windows),
  `tests/test_main.cpp` (new crash-child mode), `tests/infra/crash_runtime_tests.cpp`
  (unit + integration tests).
- No CLI, API, or dependency changes; non-Windows builds are untouched (the
  zone is inert there).
- Known ceiling (documented in design + backlog): a third-party
  `LoadLibrary` outside our guard can still raise under loader lock; if that
  ever bites, the bounded helper-thread capture sketched in design.md is the
  upgrade path.
- On completion the backlog entry "Flaky `test-parallel` hang" is marked
  resolved.
