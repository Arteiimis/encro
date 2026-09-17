## Why

`xmake test-parallel` lets Catch2 choose which cases land in which shard
(`--shard-count`/`--shard-index` slice the randomised run order). Measured
consequences on the current suite:

- Shard membership changes on every run, so a shard's assert count swings between runs
  and the printed aggregate total is not a metric: the same 770 cases reported 21113,
  11350, 7375 and 17973 assertions across runs, while a single process reports 16306
  (15594 unit + 712 e2e). A heavy section can be executed twice or not at all.
- Partitioning is cost-blind: one shard held 7364 assertions while another held 702, so
  the parallel wall clock is set by whichever shard draws the heavy cases (the suite's
  heaviest case is a ~4800-assertion synthetic ETA loop).
- Because the total is not conserved and the task never checks coverage, nothing can
  tell a sharded run that executed the whole suite apart from one that silently ran
  less, and an order-dependent failure is not reproducible from a shard log alone.

## What Changes

- The parallel test task enumerates the suite's test cases itself and gives each shard
  an explicit spec file (`-f <file>`, one escaped case name per line), replacing
  `--shard-count`/`--shard-index`. The assignment becomes a deterministic function of
  the enumeration and the cost model, and every enumerated case runs in exactly one
  shard.
- Case enumeration reads the machine-readable listing (`--list-tests -r xml`), because
  the console listing wraps long case names.
- Before starting anything, the task validates the enumeration (both listings agree on
  the count), the escaping (a comma-containing name is round-tripped through
  `--list-tests -f`), the reserved characters (a name containing `* ? [ ] ~ "` is
  rejected by name) and the assignment (every case exactly once; the shard count cannot
  exceed the case count, since an empty spec file means "run everything").
- After the run, each shard's executed case count must equal the count it was assigned,
  and a shard that produces no readable console summary is reported as failed instead of
  quietly shrinking the printed aggregate. **BREAKING**: the previous
  "per-shard counts in the shard logs" fallback is removed, because the fallback only
  existed to paper over an unaccountable partition.
- Shards are packed by measured cost (LPT over the durations the runs already record,
  persisted per suite and reused), so the heaviest shard approaches the mean instead of
  being the wall clock. The task prints one aggregate line and distribution per suite.
- `xmake test-parallel` gains `--selftest` covering the new logic (listing parse,
  escaping, reserved-character scan, cost parse and reuse, partition and assignment
  checks) with no test binaries.
- The task keeps its other contracts: per-shard JUnit verdicts, per-shard temp roots,
  `--durations yes`, the stale-binary guard, and the failure output of a failing shard.

## Capabilities

### New Capabilities

None: the parallel test task's behaviour is already owned by `deterministic-test-sync`.

### Modified Capabilities

- `deterministic-test-sync`: three added requirements - shards partition the suite by an
  explicit, validated case list (every case exactly once, loud failure on an unsound
  enumeration, escaping, assignment or empty shard); shard assignment follows measured
  per-case cost and reports the distribution; and the reported totals are the sum of what
  actually ran per suite, with an unreadable shard summary reported as a failure. The
  existing report-driven shard-verdict requirement is unchanged; the coverage checks are
  additional task-level checks.

## Impact

- `plugins/test_parallel/xmake.lua`: enumeration, escaping and validation, spec files,
  cost model and persistence, partition, coverage checks, per-suite reporting,
  `--selftest`; its header comment currently documents the `--shard-count` mechanism and
  the old speedup numbers.
- `AGENTS.md`: the parallel-tests bullet describes the current shard verdict but not the
  partition, the escaping rules or the `--selftest` command.
- No product code, no new dependencies, no change to the `--unit-shards`/`--e2e-shards`
  options, and no change to CI (which runs the suites unsharded).
