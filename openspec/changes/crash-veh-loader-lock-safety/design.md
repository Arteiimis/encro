## Context

The crash chain installed by hardening-crash-diagnostics is:
`installHandlers()` → first-chance VEH (`vehFatalHandler`) → fatal-code check
{AV, illegal instruction, stack overflow} → `writeCrashReport` →
`crash::captureStacktrace` → MSVC STL `std::stacktrace`. The STL symbolizer
(stl/src/stacktrace.cpp) is a per-process phoenix singleton that lazily loads
dbgeng, creates a debug client, attaches it to the own process, and waits
`WaitForEvent(0, INFINITE)` for the initial debug event. On a thread holding
the loader lock (inside a DLL's static initialization) that wait can never
complete — the confirmed hang (backlog "Flaky `test-parallel` hang", root
cause verified 2026-09-08 with live repro).

Two first-party code paths run DLL initialization on our threads, both after
`installHandlers()`: the CUDA-provider preload in `ensureGpuRuntimePaths`
(`LoadLibraryExW` of `onnxruntime_providers_shared.dll` /
`onnxruntime_providers_cuda.dll`, per-`OnnxTagger` construction) and the
NVIDIA driver probe `hasNvidiaDriver()` in `model_store`
(`LoadLibraryW`/`FreeLibrary` of `nvcuda.dll`, the default argument of
`ensureCudnn` reached from the `--download-models` flow on the main thread).
Exe-startup static imports initialize before `main`, i.e. before
`installHandlers()`, so they are not interceptable. `LoadLibraryW("nvcuda.dll")`
with a half-broken driver is exactly the kind of window where an init-time
fatal-code exception would hang `encro` the same way the provider preload
hangs the test suite. hardening-crash-diagnostics design D1's risk list costed
the VEH as "one integer comparison per first-chance exception" and never
considered report work running under loader lock — that is the blind spot this
change closes.

## Goals / Non-Goals

**Goals:**

- No crash report path may block on the loader lock, regardless of which
  report entry (VEH, terminate, UE filter, `reportCaughtException`) fires.
- Handled fatal-code exceptions raised inside DLL initialization produce no
  false `[CRASH]` record.
- Crash records for exceptions outside DLL-load zones are byte-for-byte what
  they are today (symbolized stack included).

**Non-Goals:**

- No change to the fatal-code set or to the first-chance VEH strategy itself
  (D1 of hardening-crash-diagnostics stands).
- No symbolization backend swap; cold symbolization outside loader lock keeps
  using MSVC STL `std::stacktrace`.
- No coverage for third-party `LoadLibrary` windows we do not own (see Risks).
- Non-Windows: the zone exists but is inert; the signal path is untouched.

## Decisions

### D1: Thread-scoped RAII zone flag consulted by the crash paths

A nestable `thread_local` depth counter in `crash_runtime`
(`ScopedDllLoadZone`), set around our explicit `LoadLibrary*` windows; the VEH
and `writeCrashReport` read it lock-free.

- Why: there is no public API answering "does the current thread hold the
  loader lock"; reentrancy probing (`Rtl*LoaderLock`) cannot distinguish
  self-held. A flag we own is the only cheap deterministic signal, and both
  explicit `LoadLibrary` sites in the binary (`ensureGpuRuntimePaths`,
  `hasNvidiaDriver`) are ours to guard.
- Alternatives considered:
  - *Pre-warm dbgeng at startup* (first `captureStacktrace` in a benign
    context): fixes the cold-init deadlock but taxes every `encro` invocation
    with a debugger-engine attach to its own process — unacceptable startup
    cost for a CLI. Rejected.
  - *Bounded helper-thread capture* (pre-spawned parked thread; VEH captures
    raw frames inline, helper symbolizes under a timeout; late full report
    when the lock frees): covers even third-party load windows, but needs a
    permanent thread, manual frame-capture/symbolize splitting (the STL API
    fuses them), and late-report semantics — over an hundred lines for a
    trigger that has exactly one call site. Rejected for now; this is the
    documented upgrade path.
  - *Report only at the unhandled-exception filter* (drop first-chance):
    D1 of hardening-crash-diagnostics already ruled this out (single filter
    slot, Catch2 displaces it, ordering battles).

### D2: In-zone VEH pass-through is silent — no record at all

In the zone, `vehFatalHandler` returns `EXCEPTION_CONTINUE_SEARCH` without
reporting, exactly as for non-fatal codes.

- Why: in-zone fatal-code raises are overwhelmingly handled probing
  exceptions (the observed provider-init AV is caught by the provider's own
  SEH). A `[CRASH]` record for a non-crash is a false alarm — the same
  principle as the existing "non-fatal exceptions are not intercepted"
  scenario. A truly unhandled one still surfaces: outside Catch2 our UE
  filter / terminate path reports it (trace-less, D3); inside Catch2 the
  third-party fatal handler terminates loudly.
- Alternative rejected: "record without trace" keeps the false alarm and
  still writes to the log under loader lock (the file-append tiers are
  lock-free, but why risk I/O on a thread that must not stall).

### D3: All report paths gate the stack capture on the zone

`writeCrashReport` skips `captureStacktrace` when the zone flag is set and
notes the omission in the message (e.g. "stacktrace skipped: DLL-load zone
(loader lock)"). This covers terminate/UE-filter/`reportCaughtException`
entries on a zoned thread (e.g. a C++ exception escaping DLL init →
`terminate`), which would otherwise hit the same dbgeng wait.

### D4: One guard per load site, not per call

The zone wraps the `LoadLibraryExW` loop in `ensureGpuRuntimePaths` and the
`LoadLibraryW`/`FreeLibrary` window in `hasNvidiaDriver`; the
`AddDllDirectory` / PATH preamble performs no DLL initialization on the
thread and stays outside. Both sites are Windows-only code paths (`#if
defined(_WIN32)` already guards them).

### D5: Testing

- **Unit (in-process, existing logger-capture harness):**
  - zone RAII set/clear/nesting semantics;
  - VEH pass-through: enter zone, `RaiseException(EXCEPTION_ACCESS_VIOLATION)`
    inside `__try/__except`, assert the local handler caught it and no crash
    record was written; same raise without the zone still records (control);
  - trace-less report: enter zone, `reportCaughtException`, assert the record
    is written, carries the omission marker, and no stack frames.
- **Integration (crash-child, bounded):** new `--encro-crash-child=dll-zone`
  mode — install handlers, enter the zone, raise AV inside `__try/__except`,
  exit 0. The parent reads the child's pipes to EOF and asserts completion
  well inside a fixed bound (pipe EOF is the no-hang proof; the pre-fix code
  would hang at the raise). This is the regression test for the backlog hang.
- **Manual verification task:** `tests.exe "[real-model]"` (model present on
  the dev box) completes instead of hanging — recorded in tasks, not CI
  (CI lacks the model; the test SKIPs there as today).

## Risks / Trade-offs

- [Crash records from zoned threads lose symbolized stacks] → By design; the
  omission marker names the reason; zone windows are milliseconds wide.
- [Third-party `LoadLibrary` outside our guard can still deadlock] → Audited
  (`rg LoadLibrary` over `src/`): the only explicit sites are the two guarded
  ones; remaining exposure is DLL initialization inside third-party libraries
  we call, documented as the zone's ceiling (`ponytail:` comment at the
  definition). Upgrade path: bounded helper-thread capture (D1 alternative).
- [A genuine crash inside our LoadLibrary window is not recorded
  first-chance] → If unhandled it still reaches the UE filter / terminate
  path and is reported trace-less (D3); the window is tiny.
- [`thread_local` access inside a VEH] → The VEH runs on the raising thread
  with normal TLS available; the flag is only set during an explicit call
  window, never during thread teardown. Reads stay lock-free per the crash
  path constraints.
- [Zone left set if an exception unwinds through it] → RAII destructor clears
  it during unwind; on non-unwinding fatal paths the process is dying anyway.

## Migration Plan

None — additive, no interface change. Rollback = revert the single
implementation commit; the zone guard is inert when nothing constructs it.
