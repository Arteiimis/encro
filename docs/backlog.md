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

## Flaky e2e `encro webp CLI can use the fake ffmpeg toolchain`

- **Status:** open · **Found:** 2026-09-08 (during console-message-conventions verification)
- **Symptom:** `tests/e2e/encro_e2e_tests.cpp` "encro webp CLI can use the fake
  ffmpeg toolchain" (spaced-toolchain section) intermittently fails with
  `exitCode == 1` and `Failed to encode: 1` in the summary — the encode itself
  failed and produced no output. Confirmed failing at HEAD without local
  changes, passing on later reruns.
- **Diagnosis so far:** not stream/badge related; the fake tool log in the kept
  temp dir needs checking (invocation log was absent from the kept dir —
  inspect the fake ffmpeg's recorded stderr/exit path and the encoded-webp
  planning step). Suspect a retry/step race in the webp size-targeting loop
  (`video_encode_runner`) under parallel-shard load.
- **Fix direction:** reproduce under `--shard-index` replay, inspect the fake
  tool log + job state in the kept temp dir, then fix the underlying race or
  fake-tool response.
- **Impact:** CI signal only; product code unaffected.
