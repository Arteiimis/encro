# Proposal: harden-test-isolation

## Why

A green run does not yet mean the same thing in every invocation mode: the suite's verdict still depends on process-global state left behind by earlier test cases, on the reporter and stdio mode, and on how a parallel run partitions and judges its shards. The 2026-09-16 CI SIGSEGV (run 35102342170, debug and coverage jobs) was one instance of that class — a test-local guard cleared four process-global completion registries that another file read, and Catch2's execution order decided whether it crashed. Catch2 v3.15 randomises that order on every run (`docs/backlog.md:83`), so an order-sensitive case does not fail once on an unlucky build; it fails intermittently forever. The instance is fixed (`e9d6cdd`); the class is not.

Evidence collected on 2026-09-16 (Windows, release build, `build/windows/x64/release/tests.exe`, full suite = 713 cases / 31827 assertions):

- The same class was fixed once before and came back in a new shape: the 2026-09-08 `video scan narration` flake (`docs/backlog.md:75-92`) was an order-dependent `StdoutCapture` polluted by a `LOG_*` fallback, patched by installing a sink-less default logger. The general form — capture safety plus restoring mutated globals — is what this change specifies.
- `logging::shutdown()` (`src/logging/setup.cpp:508`) is called from tests 25 times and takes the sink-less default logger installed in `tests/test_main.cpp:127` down with it; afterwards any `LOG_*` fallback writes to real stderr, so every "stderr is clean" capture assertion depends on which test ran first.
- `tests/utils_tests.cpp:24-63` re-implements stdout capture with a plain `_dup2` restore instead of an RAII guard: one failing assertion inside that window leaves the process's stdout redirected to a temporary file for the rest of the run (and cascade failures hide the original one).
- Running with `-r console -s` fails 5 assertions (`tests/utils_tests.cpp:119` and `:169`, `tests/infra/terminal_tests.cpp:210`) because the reporter writes the successful-assertion text into the file a test is capturing — assertions run inside the redirect window. Without `-s` the same binary is green, so the *verdict* depends on how the suite is invoked.
- `tests/cmd_cmd_tests.cpp` restores the process-global colour mode with a statement at the end of each of its 7 setter cases, so a failing assertion leaves the mode sticky; the guards that would prevent it exist but are file-local (`tests/infra/terminal_tests.cpp`, `tests/cmd_help_tiering_tests.cpp:13-15`). `tests/infra/crash_runtime_tests.cpp:175`/`:184` leaves the crash-context provider at `nullptr`, after which crash records stop naming the running test — exactly what `test-failure-diagnostics` promises.
- `xmake test-parallel` derives shard verdicts from shard log text and discards the exit status (`plugins/test_parallel/xmake.lua:96-121`), while 12 processes run concurrently with only TMP/TEMP isolated per shard.
- Fixtures still use run- or machine-specific paths: `%TEMP%/encro_test_empty` (`tests/pack_execute_tests.cpp:54`), `remove_all(%TEMP%/encro/scratch)` against the real application scratch root (`tests/work_dirs_tests.cpp:26`), a hardcoded `C:/Users/LEGION/.encro/models` (`tests/tagger/real_model_tests.cpp:17`), and `TempDir` names built from `steady_clock` alone (`tests/test_utils.h:44-45`).
- The same investigation found the timing margins that a loaded 12-shard run will hit first: a 500 ms window proving two encodes overlap (`tests/e2e/encro_e2e_tests.cpp:886`), a 60 s wait for a real-ffmpeg segment (`:2060-2085`), `FinalizingWindow::hold()` capped at 5 s against 2000 packed entries (`tests/pack_service_tests.cpp`), and a 3 s flush wait against a 1 s flusher cadence (`tests/logging_file_mgmt_tests.cpp:394`).

Doing this now is cheap: the findings are already verified with file:line evidence, no production behaviour is at stake, and the guardrails that keep the classes from returning belong in specs the same way the earlier `sleep_for` ban did.

## What Changes

- Introduce the `test-isolation` capability: tests restore every process-global they mutate; stdio capture is RAII and no assertion runs while a stream is redirected; a run's verdict and assertion count do not depend on execution order, reporter, stdio mode, or shard count; fixtures use only per-run private paths.
- Extend `deterministic-test-sync`: in-flight proofs use gates and polling instead of fixed windows; poll deadlines stay a multiple of the producer's cadence; the parallel task's shard verdicts come from each shard's own test report, with log text only as evidence.
- Extend `portable-fake-tool`: a gate deadline expiring without release becomes observable instead of silently letting the invocation proceed.
- Extend `test-failure-diagnostics`: a test that replaces the crash-context provider restores it, so crash records keep naming the running test for the rest of the run.
- CI keeps the shuffled order Catch2 already applies, but makes it observable: the unit invocation gains the console reporter alongside the JUnit report so the run's seed reaches the uploaded log (`-r console -r junit::out=/tmp/ut.xml`), and the reproduction line is documented. Without that, an order-dependent failure in CI is a dead end: the current JUnit-only invocation prints no seed.
- Guardrails: a meta-check rejects raw stream redirection and stream-buffer swaps outside the shared test helpers, in the same spirit as the existing bare-`sleep_for` check.

Non-goals:

- No production behaviour change; production files are touched only where a test seam already exists (none is expected).
- No automatic retry, no in-shard parallelism, no change to shard counts or to the existing partitioning strategy.
- No standing ASan/sanitizer gate: switching to `releasedbg` reinstalls every dependency as an ASan build, which is not affordable per commit. The one-off sanitizer sweep recorded in `docs/backlog.md` stays the reference for the historical release-job SIGSEGV.
- Not chasing the JUnit-vs-console assertion-count difference beyond making the number the tooling prints honest (`tests=31837` versus the console's `31827`).

## Capabilities

### New Capabilities

- `test-isolation`: how the suite stays independent of execution order and invocation mode — restoring mutated process-global state, RAII stdio capture with assertions outside the redirect window, invocation-independent verdicts and counts, and per-run private fixture paths.

### Modified Capabilities

- `deterministic-test-sync`: replace window-based in-flight proofs with gate/poll proofs, require poll deadlines sized to the producer's cadence, and derive parallel-shard verdicts from each shard's own report instead of log text.
- `portable-fake-tool`: make a gate deadline expiry observable so a miswired gate cannot silently pass.
- `test-failure-diagnostics`: require restoration of the crash-context provider after a test replaces it, so crash records keep naming the running test.

## Impact

- Test infrastructure: `tests/test_utils.h` (RAII capture and scoped-global guards), `tests/test_utils_tests.cpp` (meta-check coverage), `tests/test_main.cpp` (default logger installation after shutdown).
- Test bodies: `tests/utils_tests.cpp`, `tests/infra/terminal_tests.cpp`, `tests/cmd_cmd_tests.cpp`, `tests/infra/crash_runtime_tests.cpp`, `tests/infra/stop_signal_tests.cpp`, `tests/logging_*_tests.cpp` (25 `logging::shutdown()` sites), `tests/video/video_process_orchestration_tests.cpp`, `tests/work_dirs_tests.cpp`, `tests/pack_execute_tests.cpp`, `tests/pack_service_tests.cpp`, `tests/logging_file_mgmt_tests.cpp`, `tests/e2e/encro_e2e_tests.cpp`.
- Test tooling: `tests/e2e/fake_media_tool.cpp` (gate deadline observability), `plugins/test_parallel/xmake.lua` (verdicts, shard isolation).
- CI: `.github/workflows/ci.yml` (console reporter alongside the JUnit report so the run's order seed is visible).
- Documentation: `AGENTS.md` (how to reproduce a shuffled-order failure from its seed), `docs/backlog.md` if a residual flake is discovered.
