## ADDED Requirements

### Requirement: Shards partition the suite by an explicit case list

The parallel test task SHALL enumerate the suite's test cases itself and assign each
enumerated case to exactly one shard, passing that assignment to the runner as an
explicit list of case names. It SHALL NOT rely on the runner's own shard selection
over a randomised execution order. The assignment SHALL be a deterministic function of
the enumerated case list and the recorded per-case cost, and the task SHALL write the
spec files it used for the run to disk. Enumeration SHALL read a machine-readable
listing
rather than the human-readable one, and case names SHALL be passed in the spec form
the runner accepts, with the characters that syntax reserves escaped. Before spawning
any shard the task SHALL validate the enumeration, the escaping, the scan for reserved
characters and the assignment, failing with the offending count or name instead of
starting a run it cannot account for. After the run, each shard's executed case count
SHALL match the count assigned to it, and the task SHALL fail naming the shard when it
does not. A shard whose assignment would be empty SHALL NOT be started; the task SHALL
fail with guidance to lower the shard count.

#### Scenario: Every case runs in exactly one shard

- **WHEN** the task partitions an enumeration across N shards
- **THEN** every enumerated case appears in exactly one shard's assignment
- **AND** the sum of the shards' executed case counts equals the enumerated count

#### Scenario: The same inputs produce the same assignment

- **WHEN** the task partitions the same enumeration twice with the same recorded per-case cost
- **THEN** both runs produce the same assignment
- **AND** each run left the spec files it used on disk

#### Scenario: A case name needs escaping

- **WHEN** an enumerated case name contains a character the runner's spec syntax reserves
- **THEN** the name is written with the escaping that runner accepts
- **AND** a pre-run check confirms the escaped name still selects exactly that case

#### Scenario: The enumeration or an assignment is unsound

- **WHEN** the two listings disagree on the case count, or a name contains a reserved character the escaping does not handle, or the assignment misses or duplicates a name
- **THEN** the task fails before starting any shard
- **AND** it names the case count, the offending name, or the missing and duplicated names

#### Scenario: A shard executes fewer cases than assigned

- **WHEN** a shard's executed case count differs from the count assigned to it
- **THEN** the task reports that shard as failed, naming the assigned and executed counts

#### Scenario: Shard count exceeds the number of cases

- **WHEN** the requested shard count is larger than the enumerated case count
- **THEN** the task fails instead of starting a shard with an empty assignment

### Requirement: Shard assignment follows measured case cost

The parallel task SHALL assign cases to shards using measured per-case cost, so that
no shard carries a disproportionate share of the suite's work, and it SHALL report the
resulting distribution per suite. Per-case cost SHALL be derived from the durations
the runs already record, SHALL be reused across runs, and SHALL have a defined
fallback for cases it does not know yet, so a first run on a fresh checkout still
partitions the suite correctly.

#### Scenario: Cost-heavy cases are spread across shards

- **WHEN** the suite contains a case whose cost is a large share of the total
- **THEN** shard assignment places it in the least loaded shard
- **AND** the reported distribution shows no shard far above the mean

#### Scenario: No cost data yet

- **WHEN** the task runs with no recorded per-case cost
- **THEN** the partition still assigns every case to exactly one shard
- **AND** the suite completes with every case executed exactly once

#### Scenario: A recorded cost model is reused

- **WHEN** the task runs again with a recorded per-case cost model
- **THEN** the assignment uses those recorded costs instead of an equal split
- **AND** the reported distribution reflects them

### Requirement: Reported totals are the sum of what ran

The task SHALL report, per suite, an aggregate assert count equal to the sum of that
suite's shards' own console totals, together with the enumerated and executed case
counts. Its coverage check SHALL treat a shard that produced no readable console summary
as failed, in addition to whatever that shard's report says, and the task SHALL NOT print
an aggregate for such a suite. For a suite whose cases assert deterministic counts, the
aggregate SHALL equal the count a single-process run of that suite prints.

#### Scenario: All shards pass

- **WHEN** every shard of a suite passes and reports its console summary
- **THEN** the reported aggregate equals the sum of that suite's per-shard totals
- **AND** the reported executed case count equals the enumerated count

#### Scenario: A shard produces no summary

- **WHEN** a shard's console summary cannot be read
- **THEN** the coverage check reports that shard as failed
- **AND** the task does not print an aggregate total built from the remaining shards

#### Scenario: Aggregate equals a single-process run

- **WHEN** every shard of a suite whose cases assert deterministic counts passes
- **THEN** the aggregate equals the count a single-process run of that suite prints
