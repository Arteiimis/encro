# Backlog

Deferred issues, recorded with diagnosis so future fixes can skip the
investigation. Newest last.

## RESOLVED: Flaky `test-parallel` hang (unit shard stuck before first test)

- **Status:** fixed in c5977dc (`crash-veh-loader-lock-safety`: DLL-load zone
  gating the crash paths) · **Found:** 2026-09-08 (during console-message-conventions verification)
- **Symptom:** `xmake test-parallel` sometimes hangs indefinitely with one unit
  shard's `tests.exe` stuck right after the Catch2 seed line (0 tests run,
  near-zero CPU); the same binary passes all 12 shards in ~13 s on other runs.
  Reproduces deterministically for a given `--rng-seed` when the affected test
  order is replayed.
- **Root cause (cdb stack capture):** `onnxruntime_providers_cuda` raises an
  exception during its DLL static init (`ucrtbase!initterm`, provider
  discovery). The VEH crash handler installed by
  `crash::installHandlers()` (hardening-crash-diagnostics) treats it as fatal
  and calls `writeCrashReport` → `captureStacktrace`, which initializes the
  dbgeng COM service. Under loader lock that COM init deadlocks
  (`dbgeng!DebugCreateEx` → `SleepEx` forever), so the process never exits and
  the shard never finishes.
- **Root cause confirmed 2026-09-08** (source walk + live repro; supersedes
  the "dbghelp" assumption — `xmake.lua`'s `add_syslinks("dbghelp")` is a red
  herring, the import table carries no dbghelp.dll):
  - MSVC STL `std::stacktrace` (stl/src/stacktrace.cpp) symbolizes via
    dbgeng, not dbghelp: lazy `LoadLibraryExW(dbgeng.dll)` → `DebugCreate` →
    `AttachProcess(self, NONINVASIVE)` → `WaitForEvent(0, INFINITE)` — an
    indefinite debug-event wait that cannot complete from a thread holding
    the loader lock mid-DLL-init. Matches the cdb frames exactly.
  - Trigger chain: `tests.exe` imports `onnxruntime.dll`; with model files
    present (`~/.encro/models`), `real_model_tests` constructs `OnnxTagger` →
    `ensureGpuRuntimePaths` `LoadLibraryExW`s `onnxruntime_providers_cuda.dll`
    (onnx_tagger.cpp); its static init raises a first-chance AV (a fatal code
    in `isFatalExceptionCode`), so `vehFatalHandler` runs the full report on
    the loader-lock-holding thread. `captureStacktrace` is the first statement
    of `writeCrashReport`, hence zero output before the hang.
  - Seed determinism mechanism: the dbgeng engine is a per-process phoenix
    singleton behind an SRW lock — the *first* `captureStacktrace` in a
    process performs the deadlocking attach; later calls only query. A shard
    hangs iff its first test is the onnx load *and* no earlier test warmed
    the symbolizer (e.g. crash_runtime tests do); shard composition is fixed
    by `--rng-seed`.
  - Live repro (twice, different seeds): `tests.exe "[real-model]"` alone
    prints only `Filters:` + seed line and never finishes; main thread
    `WaitReason=ExecutionDelay` (SleepEx family), CPU frozen across samples
    (0.3 s → 0.3 s).
  - Design blind spot: hardening-crash-diagnostics design D1's risk list only
    costed "one integer comparison per first-chance exception" and never
    considered that first-chance handling runs the report path on threads
    that may hold the loader lock, where LoadLibrary/COM/debug-engine waits
    are forbidden. Same latent deadlock exists in `encro.exe` (it runs the
    same provider preload), not just tests.
- **Fix direction:** the VEH handler must not hijack exceptions that originate
  inside third-party DLL initialization (or at minimum must not run the
  dbgeng-backed stack capture on the loader-lock thread); consider
  first-chance/continuable filtering and a hard timeout fallback to
  `MiniDumpWriteDump` or stderr-only reporting.
- **Impact:** CI/verification reliability only; product code unaffected.

## RESOLVED: e2e `encro webp CLI can use the fake ffmpeg toolchain`

- **Status:** fixed in 1a4550d (exec2 unquoted spaced-path resolution) · **Found:**
  2026-09-08 (during console-message-conventions verification)
- **Root cause (not a race):** `5cec2fa` added the exit-127 token check to
  exec2. On Windows `quoteToolPath` emits bare paths, and the shell parse
  splits a spaced path at its first space; the new `fs::exists(argv[0])` then
  rejected the truncated token before the launcher's whitespace-extension
  search could resolve the real tool. Deterministic on Windows for spaced
  `--ffmpeg-path` roots; CI (posix, quoted paths) stayed green, which read as
  "flaky". exec2 now mirrors the launcher by accepting the first argv-prefix
  join that names an existing file (regression test:
  "exec2 resolves an unquoted tool path containing spaces").

## RESOLVED: narration tests capture log lines on stdout (order-dependent)

- **Status:** fixed in `test: keep unit-test log fallback off stdout` ·
  **Found:** 2026-09-08 (CI run 34230413061, debug job; release/coverage green)
- **Symptom:** `video scan narration prints one outcome line on non-TTY output`
  (`tests/video/video_process_orchestration_tests.cpp:307`) intermittently
  fails with `captured.find("Scanning") != npos` (plus `candidate` at `:308`),
  while re-running the same commit goes green.
- **Root cause:** Catch2 v3.15 defaults to `--order rand` with a fresh seed per
  run, so test order varies. The unit-test binary never calls
  `logging::setup()`, and `LOG_*` falls back to
  `spdlog::default_logger_raw()` (`src/logging/logging.h`), which is spdlog's
  built-in **stdout** logger. Whenever the narration test ran before the first
  test that calls `logging::shutdown()` (which resets the default logger to
  null), `LOG_INFO("Scanning input path: ...")` and
  `LOG_INFO("Scan completed: N candidate video(s)")`
  (`src/video/video_process.cpp:176,185`) landed in the test's `StdoutCapture`
  file and tripped the absence assertions. Reproduced locally: the test alone
  fails; paired with `logging::shutdown: cleans up spdlog global state` it
  fails only when the narration test runs first. ~2% per CI job — the test must
  precede the run's first `logging::shutdown()`. Same mechanism applied to
  `packer_tests.cpp:61` (`Scanning` from `src/pack/packer.cpp:756`).
- **Fix:** sink-less default logger installed in `tests/test_main.cpp`;
  regression guard is the `[test-utils][meta]` child probe in
  `tests/infra/crash_runtime_tests.cpp`. The crash-on-demand child keeps the
  stdout logger because its report is what the parent test reads.
- **Impact:** CI reliability only; product code unaffected.

## Flaky release-job SIGSEGV in the unit suite (heap corruption, victim test unrelated)

- **Status:** open — no repro; rerunning the same commit's failed job passed
  (run 34768754276 rerun) and the next push run was green (34771731216) ·
  **Found:** 2026-09-13 (CI push of da34fd5, `unify-progress-scroll`)
- **Symptom:** the `release` job's unit step exits 139. `/tmp/ut.log` ends with
  `[CRASH] fatal signal 11` plus a symbol-less stack guarded by `#00 0x... in
  libc.so.6`, and the JUnit report attaches `SIGSEGV - Segmentation violation
  signal` to `known options carry completion metadata`
  (`tests/cmd_completion_capture_tests.cpp:46`, last test of the run).
- **Diagnosis:** that test only does registry lookups, temporary-string
  compares and `malloc`/`free`; with the fault inside libc called from the test
  binary it is the first allocation after heap corruption — the victim, not the
  culprit. The capturing change (the `ProgressContext` repaint clock) never
  touches the heap off a TTY: `progressBarsAllowed()` gates every render and
  the ticker only bumps `tickCount_` under the mutex, so CI (no TTY) runs it
  inert. The crashing file is unchanged since 2026-09-03. Only release is
  affected — debug and coverage pass on the same commit — which points at a
  release-only (LTO, no `_MSVC_STL_HARDENING`) silent out-of-bounds/UAF write
  earlier in the suite; organize's ONNX Runtime inference and the pack cancel
  paths are the standing candidates.
- **Next step:** release carries no DWARF, so the captured stack cannot be
  symbolized. Rebuild with `xmake f -m releasedbg` (ASan) and run the suite
  repeatedly before re-reading the stack; a symbolized release job would make
  the next occurrence self-diagnosing.

## test-report assertion count vs the console summary (cosmetic)

`xmake test-report` falls back to the JUnit `tests=` attribute when the console
log has no `All tests passed (...) in (...) test cases` line, and the two
reporters disagree: the JUnit reporter emits one `<testcase>` per Catch2
section, so its `tests=` counts sections (850 for 713 cases at the time of
writing) and its total differs from the console assertion count (31837 vs 31827
on one and the same run). Verdicts are unaffected — the JUnit `failures=` and
`errors=` attributes agree with the console failures — and the parallel-shard
plugin takes its counts from the console summaries. Recorded so a future reader
does not chase it; the honest fix, if it ever matters, is to print the console
summary whenever it exists.

- **Deferred:** a second fixed-seed shuffled CI pass per mode. A fixed seed only
  permutes whatever order the build happened to register, so it repeats one
  permutation forever; the seed printed in `ut.log` already makes any failure
  reproducible, and every shuffled run explores new orders for free.
