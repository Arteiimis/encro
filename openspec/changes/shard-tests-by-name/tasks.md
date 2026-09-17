## 1. Enumeration, escaping and preflight

- [ ] 1.1 Add an enumeration helper that runs the suite binary's machine-readable
  listing (`--list-tests -r xml`), extracts one case name per `<TestCase>` node,
  unescapes XML entities, and cross-checks the count against the count the
  human-readable listing declares (`N test cases`). Verify: `xmake test-parallel
  --selftest` exits 0 and prints the parsed count for a fixture listing; the same
  command fails when the fixture's node count and declared count disagree.
- [ ] 1.2 Add a spec-escaping helper (`,` becomes `\,`) plus a preflight that writes the
  first enumerated comma-containing name to a one-line spec file and requires
  `--list-tests -f <file>` to report exactly one matching case (Catch2 prints the
  singular form for one match). Verify: `--selftest` covers the helper, and a run prints
  the probe result and aborts naming the offending name when the probe does not report
  exactly one match.
- [ ] 1.3 Add the reserved-character scan: a name containing `*`, `?`, `[`, `]`, `~` or
  `"` fails the task with that name, because the escaping helper does not cover those
  characters and an over-matching pattern would run a case in more than one shard.
  Verify: `--selftest` rejects a fixture name containing each reserved character and
  accepts the current suite's names.
- [ ] 1.4 Add the assignment check: after partitioning, every enumerated name must appear
  exactly once across the shard spec files, the total number of written spec lines must
  equal the enumerated count, and the shard count must not exceed the case count (an
  empty spec file means "run everything"). On mismatch the task raises an error naming
  the missing and duplicated names or the shard-count guidance. Verify: `--selftest`
  exercises a complete partition plus one missing a name, one duplicating a name and one
  with more shards than cases, and rejects the three broken inputs.

## 2. Cost model and partition

- [ ] 2.1 Parse the `--durations yes` table from a run's shard logs into a name → seconds
  map, folding section rows by exact name match against the enumeration, persist it per
  suite outside the wiped work directory (`build/.test-costs-<suite>.txt`), load it on the
  next run, and fall back to the median of known costs (or an equal split when nothing is
  known) for unlisted names. Verify: `--selftest` parses a fixture table containing both
  case rows and section rows, folds them onto the case, round-trips the model through a
  temporary file, and yields the median for an unknown name.
- [ ] 2.2 Partition by LPT over the costs (descending, ties broken by name) into the
  requested shard count, deterministically for identical inputs, and print each shard's
  case count and cost plus the max/mean ratio per suite. Verify: `--selftest` shows
  exactly-once coverage, identical output for identical inputs, and a max/mean ratio below
  1.3 on a synthetic cost set containing one dominant case.
- [ ] 2.3 Spawn each shard with its spec file (`-f <shard>.specs.txt`) instead of
  `--shard-count`/`--shard-index`, keeping the per-shard temp roots, the JUnit report,
  `--durations yes` and the stale-binary guard unchanged. Verify: `xmake test-parallel`
  exits 0; the unit suite's aggregate equals the total `build\windows\x64\release\tests.exe
  -r console` prints; the e2e aggregate and its case counts are reported and the aggregate
  lies within that suite's own run-to-run spread around the total
  `build\windows\x64\release\e2e_tests.exe -r console` prints (measured in the same session,
  because a few e2e cases assert once per observed event).

## 3. Coverage reporting and diagnostics

- [ ] 3.1 After the run, require each shard's executed case count to equal the count
  assigned to it; otherwise report that shard as failed with both counts and fail the
  task. Verify: mutation - remove one name from a shard's spec file (leaving the rest
  valid) and confirm that shard is reported failed with two different counts, then
  restore.
- [ ] 3.2 Report one aggregate per suite as the sum of that suite's shard totals, together
  with the enumerated and executed case counts, and report a shard whose console summary
  cannot be read as failed instead of printing a short aggregate. Verify: a normal run
  prints both suites' aggregates and case counts; a deliberately truncated shard log
  (move the file aside mid-run, or mutate the reader) fails that shard instead of printing
  a reduced total.
- [ ] 3.3 Add the `--selftest` option to the task menu, running all pure helpers
  (enumeration parse and cross-check, escaping, reserved-character scan, duration parse,
  model round-trip, partition and assignment checks) without building or running any test
  binary. Verify: `xmake test-parallel --selftest` exits 0 in a few seconds and prints one
  line per helper.

## 4. Verification of the change itself

- [ ] 4.1 After the change, two consecutive `xmake test-parallel` runs report a unit
  aggregate equal to the single-process unit total for that build and executed case counts
  equal to the enumerated counts for both suites, with a stable case count and no shard
  reporting zero cases. The pre-change evidence that the aggregate was unreliable is
  recorded in `proposal.md` (21113 / 11350 / 7375 / 17973 against 16306). Verify: both
  runs' aggregate and case-count lines, plus the direct single-process totals.
- [ ] 4.2 Balance: the printed max/mean shard cost is at most 1.3 for the unit suite, and
  the second run's partition reflects the recorded cost model rather than an equal split.
  Verify: the printed distributions from the first (no model) and second (model) runs.
- [ ] 4.3 Red-capability mutation: inject `CHECK(false)` into one test case, confirm the
  owning shard is reported failed (report-driven verdict intact) and the task exits
  non-zero, then restore and confirm green. Verify: the mutation run names the shard and
  its reason; the restored run exits 0.
- [ ] 4.4 Unsharded suites are unaffected: `xmake test-report` and `xmake run e2e_tests`
  pass, with the unit assertion count unchanged from before the change. Verify: both
  commands exit 0 and the counts are compared with the pre-change numbers.

## 5. Documentation

- [ ] 5.1 Update the `AGENTS.md` parallel-tests bullet: name-based partitioning, the
  escaping and validation the task performs, the coverage checks, the per-suite aggregate
  and the `--selftest` command; keep the existing verdict wording (per-shard report,
  `proc:wait` untrusted). Verify: the bullet matches the plugin's behaviour and the new
  command is listed with the other run commands.
- [ ] 5.2 Update the plugin's header comment, which still documents the `--shard-count`
  mechanism and the original speedup numbers, to describe the name-based partition,
  coverage checks and measured distribution. Leave `docs/backlog.md`'s separate
  `test-report` JUnit-vs-console count note untouched. Verify: the header matches the
  code and no stale mechanism description remains.
- [ ] 5.3 If implementation reveals drift from the design (a renamed cost file, a
  different fallback, a changed failure mode), update `design.md` and the spec delta in
  the same commit. Verify: design, spec delta and plugin agree on the enumeration source,
  the escaping and reserved-character rules, the cost fallback and the failure behaviour.

## 6. Self-check and review

- [ ] 6.1 Self-verify the change: `xmake test-parallel --selftest` green, `xmake
  test-parallel` green twice, `xmake test-report` green and `xmake run e2e_tests` green.
  Verify: every command's exit status and counts recorded.
- [ ] 6.2 Run the `code-review` skill over the change with the spec path
  (`openspec/changes/shard-tests-by-name`), triage its Standards/Spec/Leanness findings,
  fix the accepted ones, and close the loop with a fresh verification sub-agent. Verify:
  the reviewer's per-finding verdicts, with any rejected finding justified.
- [ ] 6.3 Archive the change and sync the delta specs into
  `openspec/specs/deterministic-test-sync/spec.md`. Verify: `openspec validate
  shard-tests-by-name --strict` exits 0 before archiving and `openspec validate --specs`
  exits 0 after the sync.
