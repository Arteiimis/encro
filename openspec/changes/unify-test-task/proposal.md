# Proposal

## Why

The project ships two xmake test tasks that reimplement the same machinery — the same stale-binary build guard, the same `-r console -r "junit::out=…"` reporter pair, the same Catch2 summary parsing — while giving different answers: `xmake test-report` (unit only, single process, 147 s measured) is the documented entry point for tag-filtered runs and writes the stable report paths, and `xmake test-parallel` (unit + e2e, sharded, 55-72 s measured) proves every case ran but cannot filter by tag and prints a weaker failure summary. Per-suite shard measurement on this host shows unit alone drops from ~147 s to ~40 s when sharded, so the single-process default is paying a 3.5-3.9x cost for nothing. Two tasks also means two places to fix a bug: the same build-guard regression had to be fixed twice, and `test-report`'s success line still falls back to the JUnit `tests=` attribute, which counts sections rather than cases.

## What Changes

- **One task replaces two.** `plugins/test_parallel/` is deleted and its sharding machinery moves into `plugins/test_report/`, which becomes the single test entry point. **BREAKING**: `xmake test-parallel` no longer exists; `xmake test-report --suite=all` is its equivalent.
- **Every run is parallel by default**, single suite or both. `--suite=unit|e2e|all` (default `unit`) selects what runs; an unset shard count still defaults per suite, so the default invocation is the fast one.
- **Tag filtering works in every mode.** `--tag` narrows the enumerated case set before sharding, which is what makes the opt-in `[real-model]` / `[completion]` runs shardable.
- **Stable report artifacts in every mode.** `build/last-test-report.xml` and `build/last-test-console.log` (unit) and the e2e pair are written whether the run used one shard or eight, so the documented grep-after-the-run workflow survives the shard default.
- **One failure summary, from the machine-readable report.** The per-shard log grep is deleted in favour of the JUnit `<failure>` text (case name, message), with the shard log path printed beside it.
- **Shard counts clamp instead of failing**, and a filtered run preserves the recorded cost model for cases it did not run, so a `--tag` run cannot silently shrink the packing model that later full runs depend on.
- **The 3 s force-exit watchdog stops being a landmine under a sharded default.** Measured: with 20 spinners pinned to one core, 2 of 6 runs of the stop-case surface died with exit 130 and no report (the watchdog's `ExitProcess`), because `stopsignal::gInstalled` is sticky once any case installed the handler, so every later `requestStop()` arms the real deadline. A shard that dies this way is reported as failed with no diagnostic.

## Capabilities

### New Capabilities

- `test-task`: the project's single test entry point — its invocation surface (suite selection, tag filter, shard counts), its defaults, the modes it runs, and the report artifacts it leaves behind regardless of mode.

### Modified Capabilities

- `test-failure-diagnostics`: the machine-readable report and its failure summary must hold for both suites and in every shard mode, including the aggregate file paths a post-run grep relies on.
- `deterministic-test-sync`: sharding now covers both suites through one task; a shard count above the enumerated case count clamps instead of failing; a filtered run must not truncate the recorded per-case cost model.

## Impact

- `plugins/test_report/xmake.lua`: rewritten as the merged task (absorbs the enumeration, escaping preflight, cost model, partitioning, shard spawning and report-based verdicts from `plugins/test_parallel/xmake.lua`, plus the stable-path writer and JUnit failure printer from the current task).
- `plugins/test_parallel/xmake.lua`: deleted with its directory.
- `src/infra/stop_signal.{h,cpp}`: two getters beside the existing test-hook setters, so a test guard can read the grace period and handler it found.
- `tests/test_utils.h`, `tests/test_main.cpp`: the stop-signal watchdog guard saves and restores the grace period it found instead of hard-coding 3 s, and the test process starts with a hang-guard grace period, so a starved stop case can no longer be killed mid-run.
- `AGENTS.md`: the two test-task bullets collapse into one; `docs/backlog.md`: one resolved entry recording what the sharded default can and cannot do (shard ceiling, the preview load flake).
- No product behaviour changes: the only `src/` edit is the pair of read-only test hooks above. No CI change — `.github/workflows/ci.yml` invokes the test binaries directly and keeps the same command.
