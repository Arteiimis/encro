## Context

See `proposal.md` - Why for the measured motivation. Constraints that shape the
approach:

- `plugins/test_parallel/xmake.lua` currently spawns shards with
  `--shard-count N --shard-index i`, carries the whole parent environment with
  `TMP`/`TEMP`/`TMPDIR` overridden per shard, judges each shard from its own JUnit
  report, and derives the printed totals from the shard console logs. It wipes
  `build/.test-parallel` at the start of every run.
- xmake's Lua sandbox has no XML library; the runner's machine-readable listing is
  line-oriented enough to parse without one.
- The runner is Catch2 v3.15.2. Measured behaviour: the console listing wraps case
  names (the longest name is 102 characters, and a wrapped fragment appears as its own
  indented line, which is indistinguishable from a case name); the XML listing
  (`--list-tests -r xml`) does not wrap and emits one `<TestCase>` with a `<Name>` child
  per case; a spec file (`-f`) takes one spec per line and selects the named case
  exactly when the name needs no escaping; a name containing a comma cannot be selected
  by the bare name (the comma makes it a list of alternatives that match nothing) or by
  quoting it alone (rejected as an invalid filter), while the same name written with the
  comma escaped selects exactly that one case - quoting *and* escaping it also works, and
  the escaped form alone is preferred as the minimal transformation.
- The suite's per-case assert counts are not all deterministic: the unit suite's total is
  stable across runs (15594 measured on four runs with different orders and one loaded
  run), while some e2e cases assert once per observed event and their totals vary by a
  handful of assertions between measurements (712-717 observed). That is why the
  count-equality claim in the spec is scoped to suites whose cases assert deterministic
  counts, and why this change claims *coverage* (each case exactly once, visible in the
  output) for both suites.

## Goals / Non-Goals

**Goals:**

- Deterministic membership: the assignment is a function of the enumerated case list
  and the cost model, never of the runner's randomised order, and every enumerated case
  runs in exactly one shard.
- Coverage the task can prove: the executed case counts are checked against the
  assignment, so a shard that runs fewer cases than it was given fails loudly instead of
  reporting a smaller passing suite.
- A per-suite aggregate that is the sum of what actually ran, which is what the
  archived `test-isolation` requirement about invocation-independent counts needs in
  order to hold under sharding.
- A cost-balanced partition, so the parallel wall clock stops being set by whichever
  shard draws the heavy cases.

**Non-Goals:**

- The verdict path stays as it is: a shard's pass/fail still comes from its own JUnit
  report, and `proc:wait` statuses stay untrusted. The new coverage checks are
  *additional* task-level checks (see D7).
- No change to shard counts, their defaults, or the option surface beyond adding
  `--selftest`; no in-shard parallelism; no change to any test case.
- No change to CI (it runs the suites unsharded) and no attempt to reconcile the
  separate `test-report` JUnit-vs-console count note in `docs/backlog.md`.

## Decisions

**D1 - Enumerate with `--list-tests -r xml`.** One `<TestCase>`/`<Name>` pair per case,
no line wrapping. Rejected: the console listing (wraps, and its wrapped fragments look
like extra case names - this produced phantom names during investigation); running the
suite once with the XML reporter to harvest names (correct but pays a full run before
sharding).

**D2 - Transport the assignment as one spec file per shard (`-f <file>`).** The 723
unit plus 47 e2e names total ~39k characters with a 102-character maximum, so passing
them in argv would need per-name escaping through the environment/argv boundary and
would approach Windows' command-line limit as soon as a partition is unbalanced.
Rejected: names in argv; tag-based selection (tags are shared by many cases, so they
cannot express a partition).

**D3 - Escape only what the runner reserves: `,` becomes `\,`.** Measured against
v3.15.2: a comma-free name selects exactly one case, with or without quotes; a name
containing a comma is *not* selectable by the bare name (the comma splits it into
alternatives that match nothing) or by quoting it alone (rejected as an invalid filter);
quoting *and* escaping it does select the one case, as does escaping it alone - the
escaped form is preferred because it is the minimal transformation verified end-to-end
for all 723 unit and 47 e2e names. Today 16 of 723 unit names contain a comma and none
contains `*`, `?`, `[`, `]`, `~` or `"`. Rejected: quoting
(does not rescue a comma name); wildcard patterns built from comma-free slices (only
accidentally unique).

**D4 - Validate before spawning: four cheap checks.** (a) The XML listing's case count
must equal the count the console listing declares, catching a format change in either.
(b) An escape round-trip: the first enumerated name containing a comma is written to a
one-line spec file, and `--list-tests -f <file>` must report exactly one matching case -
this is what catches escaping or spec-syntax drift on a runner upgrade. (c) A
reserved-character scan: any name containing `*`, `?`, `[`, `]`, `~` or `"` fails the
task with the offending name, because the escaping helper does not cover those
characters and an over-matching pattern would silently run cases in more than one shard.
(d) The assignment must cover the enumeration exactly once, and the shard count must not
exceed the enumerated case count (an empty spec file means "no filter" to the runner and
would execute the whole suite in that shard). These exist because a *valid but
unmatched* spec line is silently skipped by the runner, so the suite would simply run
smaller and still report success - syntactically invalid lines do fail loudly, which is
why the round-trip probe (b) is about syntax and the assignment checks (d) about
coverage.

**D5 - Cost model: reuse the durations the runs already print.** Every shard log ends
with the `--durations yes` table (`<seconds> s: <name>`), which also contains rows for
sections, so costs are folded by exact name match against the enumeration (a section row
whose name equals some case name is indistinguishable today; there are no such
collisions, and the fold is only a heuristic). The model is persisted per suite outside
the wiped work directory (`build/.test-costs-<suite>.txt`) and *reused* on later runs; a
name with no recorded cost gets the median of the known costs, and a model that is
absent or entirely stale degrades to an equal split by case count. Rejected: a dedicated
timing pass (doubles the suite cost); using assert counts as the cost proxy (ignores the
I/O- and process-heavy cases, which are exactly the real-ffmpeg ones).

**D6 - Partition with LPT (longest processing time first) by cost descending**, ties
broken by name so the assignment is a deterministic function of (enumeration, cost
model). Shard count still comes from `--unit-shards`/`--e2e-shards`, and the task prints
per-shard case count and cost plus the max/mean ratio per suite. Membership may therefore
shift between runs when the cost model changes; what is stable and what a failure report
can rely on is that the assignment is recomputed from inputs this run wrote to disk, not
drawn from a random order. Rejected: round-robin in enumeration order (cost-blind,
reproduces the measured 7364-vs-702 assertion imbalance); work stealing (needs
inter-process coordination for a partition LPT already keeps within ~11/9 of optimal).

**D7 - Report per-suite aggregates, and make an unreadable count a failure.** With
exactly-once membership the sum of a suite's shard totals is that suite's real total, so
the task prints one aggregate line per suite plus the enumerated and executed case
counts. A shard whose console summary cannot be read is reported as failed (its report
is still what decides pass/fail per the existing requirement; the missing summary is
treated as a health signal, and no aggregate is printed for that suite) - this replaces
today's "per-shard counts in the shard logs" fallback, which existed only because the
old partition could not account for what ran. The per-shard *case* counts are what makes
the aggregate trustworthy rather than merely printed.

**D8 - Add `--selftest` for the new logic**, following the precedent of
`plugins/tidy/scan.py --selftest`: it exercises the listing parse with entity
unescaping on a fixture string, the comma escaping, the reserved-character scan, the
duration-table parse (including section rows), the model load/save cycle, the LPT
partition (exactly-once coverage, determinism for identical inputs, and balance on a
synthetic cost set containing one dominant case), and the assignment checks on a
complete partition plus one missing a name and one duplicating a name. It needs no test
binaries and runs in seconds.

## Risks / Trade-offs

- [A runner upgrade changes `-f` or spec syntax] -> D4's round-trip probe and count
  checks fail the task with a message naming the cause, instead of running fewer cases.
- [A future case name uses another reserved character] -> the preflight rejects it by
  name (D4c); extending the escaping helper is then a one-line change, not a silent
  shortfall or an over-matching pattern.
- [Cost-driven repartitioning moves a case between shards on a later run] -> accepted:
  coverage and the aggregate stay correct, and each run writes the spec files it used, so
  a shard failure is still attributable and reproducible from that run's inputs.
- [Duration data is noisy, or a run is loaded] -> the model is a heuristic refreshed
  every run; correctness (coverage, conservation) does not depend on it, only balance
  does.
- [Spec-file encoding] -> files are written UTF-8; the count checks (D4d, D7) catch any
  name that fails to round-trip, whatever the cause.
- [Plugin state outside the work directory] -> `build/.test-costs-*.txt` is optional
  cache: deleting it costs balance for one run, never correctness.

## Migration Plan

Single commit touching `plugins/test_parallel/xmake.lua`, its header comment, and
`AGENTS.md`; no product code, no data migration. Rollback is a revert of that commit,
which restores the `--shard-count` path. The first run after the change has no cost model
and falls back to an equal split (D5).

## Open Questions

- Whether the e2e suite's 47 cases are numerous enough for cost packing to matter: the
  per-suite distribution the first runs print answers it without changing the approach.
- Whether to cache the enumeration between runs: it costs a fraction of a second today,
  so this is a convenience question, not a correctness one.
