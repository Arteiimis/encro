# Backlog

Deferred issues, recorded with diagnosis so future fixes can skip the
investigation. Newest last.

## Flaky `test-parallel` hang (unit shard stuck before first test)

- **Status:** open · **Found:** 2026-09-08 (during console-message-conventions verification)
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
