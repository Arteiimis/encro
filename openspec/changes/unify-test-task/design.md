# Design

## Context

See proposal.md — Why. The current state that shapes the approach:

- `plugins/test_report/xmake.lua` (117 lines) owns the build guard, the `-r console -r "junit::out=…"` pair, the stable `build/last-test-report.xml` / `build/last-test-console.log` paths, the JUnit `<failure>` printer, and `--tag`.
- `plugins/test_parallel/xmake.lua` (777 lines) owns enumeration from the machine-readable listing, the escaping preflight, the per-case cost model, LPT partitioning, shard spec files, private `TMP/TEMP/TMPDIR` roots, per-shard JUnit verdicts, and `--selftest`.
- Both carry an identical `run()` helper and an identical "refuse to run a stale binary" guard.
- Measured on this host (16 cores, release, incremental build included): unit single process 146.7 / 148.4 s; 8 unit shards 37.9-42.7 s across ten runs; unit+e2e at 8+4 shards 55-72 s; e2e single process 115.4 s.
- The suites' wait deadlines behave as hang guards only up to roughly the host's core count: at 8+8 shards (16 concurrent processes) a **unit** shard went red on `tests/preview/preview_process_tests.cpp:558` (`REQUIRE(started)`, `tests/preview/*.cpp` is unit-suite code), whose 10 s marker wait ran out; under 20 spinners pinned to one core that same case failed 5 of 6 times.
- The stop-signal force-exit watchdog is armed by any `requestStop()` once a handler was installed in the process, and `tests/test_utils.h` hard-codes restoring a 3 s grace period; a stop case whose tail exceeds the grace is killed by `ExitProcess(130)` with no report — measured 2 of 6 times under single-core starvation.
- `src/infra/stop_signal.h` exposes only *setters* for the two test hooks; the grace period and handler themselves are file-static in the .cpp.

## Goals / Non-Goals

**Goals:**

- One task, whose default invocation is the fast sharded unit run, and whose flags cover everything the two tasks did.
- The documented report paths and the failure summary keep working in every mode.
- A sharded default that cannot turn a starved stop case into a false failure.

**Non-Goals:**

- Changing the product's exit codes or stop-signal behaviour in `src/` (the grace fix is test-side only).
- Merging the shard JUnit reports with an XML library, or summing their attributes to rebuild one `<testsuite>`.
- Changing the CI job, which invokes the test binaries directly.
- Rewriting history: the 104 archived change documents that name `test-parallel`/`test-report` describe what landed when they landed, and `docs/backlog.md`'s resolved entries stay as written; only the new backlog entry and the current documentation move to the merged task.
- Raising the shard-count ceiling: the measured ceiling is what the suites' deadlines support.

## Decisions

### D1: Keep the `test-report` task name; suite selection is a flag

The surviving task keeps the name `test-report` and gains `--suite=unit|e2e|all` (default `unit`).

`test-report` is the name in AGENTS.md, in several archived task lists, and in the local workflow that greps the report afterwards; it also does not imply a mode, which `test-parallel` would (a single-suite run on one shard is not "parallel"). Alternatives considered: keeping `test-parallel` (worse name, and the report paths are the more-used contract); keeping a thin alias task forwarding to the merged one (re-introduces the two-entries-in-`xmake --tasks` problem this change removes).

Options: `--tag` (unchanged), `--shards=N` (sets both suites), `--unit-shards`, `--e2e-shards`, `--selftest`.

### D2: Every suite is sharded by default, with the current per-suite defaults

Defaults stay `clamp(cores/2, 1, 8)` for unit and `clamp(cores/4, 1, 4)` for e2e — the values `test-parallel` already uses. On this 16-core host that is 8 unit shards (measured 37.9-42.7 s versus 147 s single-process) and 4 e2e shards; both suites at once total 12 concurrent processes, inside the 16-core ceiling measured above.

Rejected alternative: single-suite runs defaulting to one shard (the previous `test-report` behaviour) — that is the 3.5-3.9x cost this change exists to remove.

### D3: The tag filter narrows enumeration, not the shard spec

The filter is passed to both enumeration invocations as a Catch2 test spec (`<binary> <filter> --list-tests -r xml` and `… -r console`), so the case list the task shards is already filtered and the two listings the soundness check compares describe the same set — the console one then declares `N matching test cases` instead of `N test cases`, which the check learns to read. Shard spec files keep carrying case names only, so the existing escaping preflight covers them unchanged.

Consequences handled explicitly: the requested shard count is clamped to the enumerated case count (a `--tag="[real-model]"` run selects one case), and when both suites are selected and the filter matches nothing in one of them, that suite is skipped with a notice instead of failing the run.

### D4: One report path per suite; sharded runs write an aggregate there

Paths: unit → `build/last-test-report.xml` and `build/last-test-console.log` (unchanged); e2e → `build/last-e2e-report.xml` and `build/last-e2e-console.log`.

With more than one shard, the task writes each shard's own report and log under the run directory (`build/.test-parallel/<suite>/shard-N.{xml,log}`) and then builds the documented file: the report aggregate splices every shard's `<testsuite>…</testsuite>` block under one `<testsuites>` root, and the console aggregate concatenates the shard logs in shard order. With one shard the shard file *is* the documented file (written directly to that path).

Rationale: the JUnit here is read by humans, by agents grepping `<failure>`, and by the task's own `failures=`/`errors=` verdict; a splice preserves each shard's own counts and needs no XML dependency or attribute arithmetic. Rejected alternative: rewriting one merged `<testsuite>` with summed attributes — more code, and it would discard the per-shard filter/seed properties that make a failure reproducible.

### D5: Counts come from the shard console summaries; the JUnit `tests=` fallback is deleted

Each shard's log is parsed with the summary parser `test_parallel` already has (singular, plural and totals-table forms) and the suite aggregate is the sum. The current fallback — reading `tests=` off the report when the console summary is absent — is removed: JUnit emits one `<testcase>` per section, so that attribute counts sections (850 for 713 cases at the time `docs/backlog.md` recorded it) and reports a number that disagrees with the console.

Consequence: the success line becomes `unit: all tests passed (N assertions in M/M test cases)` in every mode, single-shard included, so the format does not depend on the mode. It keeps the `assertions` and `test cases` words, and the number is now the console total rather than a section count.

### D6: Verdicts stay report-based; failure text comes from the report

Each shard is judged by its own JUnit report (`failures="0" errors="0"`) plus the executed-count check against its assignment; a missing or unreadable report fails the shard. On failure the task prints each `<failure>` block's case name and message from the shard report — Catch2 puts the failing assertion's `file(line): FAILED:` line at the head of that message, which is where the source location comes from — followed by that shard's log path. The log-grepping loop in `test_parallel` (`FAILED` lines, capped at 8) is deleted: it printed less information than the report already holds.

### D7: Every shard gets a private temp root, single-shard runs included

Each shard runs with `TMP/TEMP/TMPDIR` under its own shard directory, whatever the shard count. Keeping today's `test-report` behaviour (inherit the environment when there is one shard) would make the suite's environment — and with it the e2e assertion totals that `docs/backlog.md` records as temp-root sensitive — depend on the shard count, which `test-isolation` forbids; one code path is also what this change is for. The cost: a preserved failure scratch directory now lives under the run directory, which the task wipes at the start of the next run, so evidence survives until then (the task prints the paths).

### D8: The cost model merges instead of being rewritten from the run's enumeration

Saving currently writes exactly the cases in `plan.names`, so a `--tag` run truncates the model to the filtered subset. The save step becomes: load the existing model, overwrite entries measured this run, keep entries the model has for cases outside this run's enumeration, and drop entries whose case no longer exists in the enumeration (where a full enumeration is available). The path (`build/.test-cost-<suite>.txt`) and format are unchanged.

### D9: Test-side fix for the force-exit watchdog, so a sharded default cannot be killed

`testutils::ScopedStopSignalWatchdog` hard-codes restoring a 3 s grace period *and* restoring the real `ExitProcess` handler, which is what makes any later `requestStop()` in the same process a live 3 s deadline. Saving the previous values needs getters, so `src/infra/stop_signal.h` gains `forceExitGracePeriodForTest()` and `forceExitHandlerForTest()` beside the two setters (read-only test hooks, no product behaviour change). The guard then saves what it finds and restores it (the idiom `ScopedEnvVar` already uses), and `tests/test_main.cpp` sets a hang-guard grace period (60 s) at start-up on Windows. The watchdog case still sets its 50 ms grace explicitly and keeps passing.

Regression test: a `_WIN32` case in `tests/infra/stop_signal_tests.cpp` sets a 50 ms grace, lets a guard destruct, and asserts the grace reads back as the process default rather than 3 s — red before the fix, green after.

Rejected alternatives: editing the stop-requesting test cases to raise a stop only at the very end (20+ sites, easy to miss, and does not fix starvation); raising the product's production grace (`src/` behaviour change for a test-only problem).

## Risks / Trade-offs

- **[Aggregation depends on each shard report holding exactly one `<testsuite>` block]** → Catch2's JUnit reporter always writes one per file; if that ever changes the aggregate becomes invalid XML rather than silently wrong, and the per-shard files remain for inspection.
- **[String-spliced aggregate]** → The spliced file is valid but not pretty-printed; consumers that re-serialize it are unaffected.
- **[The success line's format changes for the default invocation]** → Anything grepping `All tests passed (` exactly would break. Mitigation: the aggregate line keeps `all tests passed`, `assertions` and `test cases`; AGENTS.md and the backlog entry are updated in the same commit.
- **[A dead shard leaves the aggregate incomplete]** → The shard is already reported failed before aggregation, so the run's verdict is unaffected; the aggregate is best-effort evidence.
- **[Raising `--e2e-shards` re-introduces the load flake]** → Documented in `docs/backlog.md` with the measured ceiling; the defaults stay inside it.
- **[Private temp roots now apply to single-shard runs too]** → `test-isolation` asks for a verdict and count identical across shard counts, and the e2e suite's assertion totals are already recorded as non-conserved across temp roots in `docs/backlog.md`; verdicts and case counts stay exact, which is what the shard-count check relies on, and the residual assertion-count variance is pre-existing rather than introduced here.
- **[Deleting `plugins/test_parallel/` breaks local habits]** → The task prints the equivalent invocation (`--suite=all`) in its help text; AGENTS.md is updated in the same commit.

## Migration Plan

1. Land the watchdog grace fix first (test-side, independently green) so the sharded default is not measured against a landmine.
2. Merge the machinery into `plugins/test_report/xmake.lua`, keeping `test_parallel` intact until the merged `--selftest` and a full `--suite=all` run are green.
3. Delete `plugins/test_parallel/` in the same commit as the merge, and update AGENTS.md.
4. Rollback: `git revert` restores both plugins in one step, because the merge is a single commit.

## Open Questions

None that would change the specs, the approach or the task breakdown.
