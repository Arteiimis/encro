## Context

The unit suite is one process (713 cases, 31827 assertions, ~110 s in release on Windows) plus a separate e2e binary; Catch2 v3.15 randomises the execution order on every run (`docs/backlog.md:83`, reproduced locally: eight consecutive runs of one tag slice produced eight different orders), and `xmake test-parallel` runs up to 12 shard processes on a 16-core machine (8 unit + 4 e2e by its clamp rules) with per-shard TMP/TEMP only. See proposal.md - Why for the verified findings. Three constraints shape the approach:

- Test helpers already exist and are the right home for the fix: `tests/test_utils.h` holds `TempDir`, `ScopedEnvVar`, `ScopedStopSignalReset`, `ScopedSyntheticJobClock`, `waitUntil`, and the RAII `FileCapture`; the colour-mode guard still lives inside `tests/infra/terminal_tests.cpp` and `tests/cmd_help_tiering_tests.cpp`. The repo's existing meta-checks (bare-`sleep_for` ban, assert-free cases) live in `tests/test_utils_tests.cpp` and locate sources through the `ENCRO_TEST_SOURCE_DIR` compile definition.
- The change may not alter product behaviour: production files stay untouched, and test seams are used only where they already exist (`crash::setCrashContextProvider`, `jobstate::setClockForTest`, `terminal::configure`, `stopsignal` test hooks).
- Both platforms must keep working: Windows (clang-cl, `_MSVC_STL_HARDENING`) is the primary development platform; Linux runs the debug/release/coverage CI matrix, and the crash tests there intentionally terminate processes.

## Goals / Non-Goals

**Goals:**

- One invocation mode cannot change another test case's outcome: mutated globals are restored, stdio capture cannot leak a redirect or absorb reporter bytes, and fixtures never share a path between concurrent runs.
- A single failing assertion stays a single failing assertion, instead of poisoning the rest of the run through the watchdog, a redirected stream, or a dangling stream buffer.
- Order dependence is caught on the first CI run rather than by accident, and is reproducible from the seed printed in the log.
- Guardrails that fail the suite when the classes return, in the same spirit as the existing `sleep_for` check.

**Non-Goals:**

- No sanitizer gate, no retries, no in-shard parallelism, no shard-count or partitioning changes.
- No new production test seams beyond the ones already present.
- Not normalising the remaining legacy style of tests that do not touch these classes.

## Decisions

### D1 - Scoped guards for process-global state, added to `tests/test_utils.h`

Add the missing guards next to the existing ones: a crash-context guard that restores the provider, and `shutdownLogging()`, a wrapper that calls `logging::shutdown()` and immediately re-installs the run's sink-less default logger. The shutdown *destroys* that logger rather than mutating it, so restoring at the call leaves nothing to restore at scope end; all 24 test call sites go through the wrapper. The colour-mode guard moves out of `tests/infra/terminal_tests.cpp` into `tests/test_utils.h` so `tests/cmd_cmd_tests.cpp` can drop its 7 statement-based sets and 9 resets; the existing `ScopedStopSignalReset` and `ScopedEnvVar` get used at the sites that still reset by statement, plus a Windows-only `ScopedStopSignalWatchdog` for the force-exit hooks the watchdog case arms (`tests/infra/stop_signal_tests.cpp:103-124`).

Alternatives: making `logging::shutdown()` itself leave a sink-less logger (rejected - that is production teardown behaviour, and a CLI process exits right after it); a `std::cin.rdbuf` guard reusing `ScopedEnvVar`'s idiom (accepted, new small guard, because five `utils_tests.cpp` cases restore by statement and one throw away from a dangling stream).

### D2 - One RAII capture helper, and no assertions inside a redirect window

`tests/utils_tests.cpp:24-63` re-implements stdout capture on raw `_dup2`. Move that helper into `tests/test_utils.h` on top of the existing `FileCapture` (RAII, restore in the destructor), delete the local copy, and move every assertion out of the window in `tests/utils_tests.cpp` and `tests/infra/terminal_tests.cpp` (assert on the captured text after the redirect ends; bind results to variables inside the window instead of checking them there). `FileCapture` itself becomes assert-free: its constructor records whether the descriptor was duplicated and the capture file opened, exposes that state for the caller to check after the redirect, and `captureStdout` performs the check after its own capture scope has ended. That last part is what makes nested captures safe: today the second capture's constructor `REQUIRE`s run inside the first capture's window, which is how `test_utils.h(304): PASSED:` ended up inside a captured file under `-s`.

Alternatives: teaching the reporter to stay quiet during a capture (rejected - no Catch2 API for it); filtering reporter lines out of the captured text (rejected - the text shape then depends on the reporter, which is the bug); keeping two capture implementations (rejected - one of them is the one that leaks); keeping assertions in the constructor and forbidding nested captures by convention (rejected - the convention is exactly what the meta-check cannot see, and the failing shape is silent).

### D3 - Guardrail meta-check for redirection and environment mutation

Extend the existing meta-check family in `tests/test_utils_tests.cpp` with a source scan (over `ENCRO_TEST_SOURCE_DIR`) that rejects raw `dup2`/`_dup2`, `freopen`, `std::cin.rdbuf(`, `std::cout.rdbuf(`, and process-wide environment setters outside `tests/test_utils.h`, unless the line carries an `// isolation-ok: <reason>` marker (mirroring the existing `// sleep-ok:` marker and its allowlist). The check scans `*.cpp` and `*.h` under `tests/` so a helper in a header cannot hide an unmarked redirect.

Alternatives: relying on review (rejected - the analogous `sleep_for` problem regressed until a check existed); a runtime instrumentation hook (rejected - cannot observe a failure path from outside the process that owns the fd).

### D4 - Parallel shard verdicts from the shard's own JUnit report plus its exit status

`plugins/test_parallel/xmake.lua:96-121` currently reads shard log text and discards the process status. Each shard is launched with Catch2's JUnit reporter into a per-shard report file (`-r console -r junit::out=<shard>.xml`), and the verdict is: failed when the process status is non-zero, failed when the report is missing or unparsable (a shard that died mid-run), otherwise passed when the report shows no `<failure>`/`<error>` entries. Log text stays as supporting evidence for the printed summary only. This mirrors the existing `test-report` plugin's JUnit idiom, works on both platforms, and does not depend on `proc:wait()`'s status reliability under many concurrent waits.

Alternatives: trusting `proc:wait()` statuses (rejected - the plugin's own comment documents them as unreliable with 12 concurrent waits); deriving the verdict from a summary line in the log (rejected - the `FAILED` substring hazard remains, and a mid-run crash is indistinguishable from a completed run); a shell wrapper writing the exit code to a file (rejected - platform-divergent quoting for no gain).

### D5 - Windows and margin fixes by rule, not case-by-case

Replace window-based in-flight proofs with gate-based ones (`tests/e2e/encro_e2e_tests.cpp:886` overlap check), widen poll deadlines to at least three producer periods (`3 s -> 10 s` flush wait, `5 s -> 30 s` finalizing hold, `60 s -> 180 s` real-ffmpeg segment wait), and keep the positive assertion in each case. `FinalizingWindow::hold()` releases on the test's signal or the widened deadline, so the file-count heavy case stops racing the release.

Alternatives: raising only the deadline that fails today (rejected - the other two fail next under a loaded run); lowering the workload (rejected - the point is the 2000-entry path).

### D6 - Per-run private fixture paths

`TempDir` derives its name from the process id and a per-process counter in addition to the clock (`tests/test_utils.h:44-45`), so two shards or two suites started in the same tick cannot share a directory. `tests/pack_execute_tests.cpp` uses a `TempDir` instead of `%TEMP%/encro_test_empty`; `tests/work_dirs_tests.cpp` pins TMP/TEMP with `ScopedEnvVar` (the application resolves its scratch root through the temporary directory) instead of deleting the shared application scratch root; `tests/video/video_process_orchestration_tests.cpp` watches a `-progress` file in its own working directory and asserts on it from the case body instead of a destructor; `tests/tagger/real_model_tests.cpp` reads the model directory from a documented environment variable and skips when it is unset, instead of hardcoding one user's home.

Alternatives: adding a production environment override for the scratch root (rejected - product change out of scope, and TMP/TEMP isolation already exists for the app); leaving the hardcoded model path (rejected - it is unusable on any other machine).

### D7 - CI makes the order seed visible instead of choosing an order

Catch2 already randomises the order, so the change is not "add shuffling" but "stop hiding the seed": `.github/workflows/ci.yml` runs the unit binary with `-r console -r junit::out=/tmp/ut.xml > /tmp/ut.log 2>&1` (the form the `test-report` plugin already uses) so the log carries `Randomness seeded to: <N>`, the console summary and per-test output, while the JUnit report keeps feeding the failure-name greps and the uploaded artifacts. `AGENTS.md` gets the reproduction line (`tests.exe --rng-seed <N>`).

Alternatives: pinning `--order decl` in CI (rejected - it hides exactly the failures this change exists to prevent, and the local runs would still shuffle); a fixed seed (rejected - it permutes whatever registration order the build produced, so it explores one permutation per artifact and never the space); leaving CI as is (rejected - an order-dependent failure there is currently a dead end, because the JUnit-only invocation prints no seed).

## Risks / Trade-offs

- [Widened deadlines hide a real hang] -> Deadlines stay hang guards with a loud failure message, and every affected case keeps its positive observable assertion; the specs require the deadline to be a multiple of the producer's cadence.
- [Shuffled CI order turns a latent bug into an intermittent red] -> That is the intended detection, and it is the status quo Catch2 already ships; the seed is printed so any red is reproducible, and the fallback if it proves too noisy is a fixed seed for one run rather than reverting to a hidden order.
- [The order is already random, so widening deadlines could mask an order bug] -> The executed-assertion-location comparison and the seed in the log keep the coverage honest; local verification compares assertion locations across runs.
- [Meta-check false positives block unrelated work] -> The check accepts an explicit `// isolation-ok: <reason>` marker, and its allowlist lives in `test_utils.h` next to the guards.
- [Per-shard JUnit reports grow `build/`] -> Reports are written under the shard work directory, which is already gitignored, and the CI collect job already uploads test reports.
- [Moving assertions out of capture windows changes what a case asserts] -> The assertions keep their exact expectations; only the reporter bytes disappear from the captured text, which is what the specs require.

## Open Questions

- Whether CI should additionally run one fixed-seed shuffled pass per mode, trading wall clock for stronger order coverage; the current approach changes no task and no requirement.
- Whether the `test-report` plugin should print the console summary's assertion count explicitly instead of falling back to the JUnit `tests=` attribute, which differs by 10 in the current build (`tests=31837` versus `31827`); cosmetic, and orthogonal to this change's specs.
