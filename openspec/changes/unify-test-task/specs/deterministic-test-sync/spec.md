# Spec Delta

## MODIFIED Requirements

### Requirement: Shards partition the suite by an explicit case list

The parallel test task SHALL enumerate the suite's test cases itself and assign each enumerated case to exactly one shard, passing that assignment to the runner as an explicit list of case names. It SHALL NOT rely on the runner's own shard selection over a randomised execution order. The assignment SHALL be a deterministic function of the enumerated case list and the recorded per-case cost, and the task SHALL write the spec files it used for the run to disk. Enumeration SHALL read a machine-readable listing rather than the human-readable one, and case names SHALL be passed in the spec form the runner accepts, with the characters that syntax reserves escaped. Before spawning any shard the task SHALL validate the enumeration, the escaping, the scan for reserved characters and the assignment, failing with the offending count or name instead of starting a run it cannot account for. After the run, each shard's executed case count SHALL match the count assigned to it, and the task SHALL fail naming the shard when it does not. A requested shard count larger than the enumerated case count SHALL be clamped to that count, because a filtered run legitimately selects fewer cases than the default shard count; no shard SHALL ever be started with an empty assignment.

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
- **THEN** the task clamps it to the enumerated count and reports the effective count
- **AND** every enumerated case runs in exactly one of the shards it started

### Requirement: Shard assignment follows measured case cost

The parallel task SHALL assign cases to shards using measured per-case cost, so that no shard carries a disproportionate share of the suite's work, and it SHALL report the resulting distribution per suite. Per-case cost SHALL be derived from the durations the runs already record, SHALL be reused across runs, and SHALL have a defined fallback for cases it does not know yet, so a first run on a fresh checkout still partitions the suite correctly. A run that executes only a subset of the suite SHALL preserve the recorded costs of the cases it did not run, so a filtered run cannot shrink the model later full runs are packed by.

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

#### Scenario: A filtered run does not truncate the model

- **WHEN** a run executes a filtered subset of the suite
- **THEN** the recorded costs of the cases outside that subset are still present afterwards
- **AND** the costs measured during the filtered run replace the entries for the cases it ran

#### Scenario: A full run prunes the deleted cases

- **WHEN** a run enumerates the whole suite and a case recorded in the model is no longer in that enumeration
- **THEN** that case's entry is dropped from the model
