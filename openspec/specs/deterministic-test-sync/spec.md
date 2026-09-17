# deterministic-test-sync Specification

## Purpose

Defines how the test suites synchronize with asynchronous and subprocess activity so that test outcomes depend on observable state, never on wall-clock timing assumptions, keeping `xmake test-parallel` runs deterministic under machine load.

## Requirements

### Requirement: Tests synchronize on observable state, not fixed delays

Tests that must wait for asynchronous activity (a subprocess invocation starting, a background thread observing a value, a file appearing) SHALL poll an observable artifact until a predicate on it holds. Test correctness SHALL NOT depend on a fixed sleep placing the test inside a timing window, and SHALL NOT include negative assertions that race asynchronous effects (asserting an effect has not happened yet at an arbitrary moment).

#### Scenario: Stop raised inside a proven in-flight invocation

- **WHEN** a test must raise a stop request while a specific fake-tool invocation is in flight
- **THEN** the test polls the invocation log until that invocation is recorded before raising the stop
- **AND** the stop cannot arrive before the invocation starts regardless of machine load

#### Scenario: Async effect awaited by polling the monitored state

- **WHEN** a test must wait for a background monitor to observe a value before changing an input
- **THEN** the test polls the monitor's shared state until the awaited value appears instead of sleeping a fixed interval

#### Scenario: Periodic-flush behavior asserted only positively

- **WHEN** a test verifies that a periodic flusher lands log lines on disk without shutdown
- **THEN** it polls for the line's appearance and does not assert the line's absence at a specific earlier moment

### Requirement: Shared poll helper treats deadlines as hang guards

The test utilities SHALL provide one shared poll helper used by both suites that repeatedly evaluates a predicate until it holds or a deadline expires, and returns whether the predicate held. A false return SHALL be surfaced by an assertion at the call site that names the awaited condition (no silent pass). Deadline values SHALL be sized for saturated parallel runs (hang protection only, never correctness margins).

#### Scenario: Predicate becomes true before the deadline

- **WHEN** the polled condition becomes true within the deadline
- **THEN** the helper returns true without waiting for the deadline to expire

#### Scenario: Predicate never becomes true

- **WHEN** the polled condition never becomes true
- **THEN** the helper returns false after the deadline and the caller's assertion fails, naming the awaited condition

### Requirement: Bare synchronization sleeps are rejected by a meta-check

The unit suite SHALL include a check that scans test sources for fixed-delay sleeps and fails the run when a sleep appears outside an allowlist. Measurement sleeps (lower-bound elapsed assertions that include a real delay) MAY remain by entering the allowlist with a marker comment.

#### Scenario: New bare synchronization sleep introduced

- **WHEN** a test adds a `sleep_for` call that is not in the allowlist
- **THEN** the meta-check fails, naming the file and line

#### Scenario: Allowlisted measurement sleep

- **WHEN** a test uses a `sleep_for` solely as a measured lower bound and is allowlisted with the marker
- **THEN** the meta-check passes

### Requirement: Elapsed-accumulation tests drive a controlled clock

Where a test asserts accumulated elapsed time through the job-state store (mark-running / mark-interrupted accumulation), the store SHALL expose a test-only clock override following the codebase's existing test-hook idiom, and such tests SHALL push synthetic timestamps instead of sleeping against the real system clock, so accumulation arithmetic is verified exactly and immune to clock adjustments.

#### Scenario: Synthetic clock advances between marks

- **WHEN** a test sets the synthetic clock, marks a task running, advances the synthetic clock by a fixed amount, and marks the task interrupted
- **THEN** the persisted accumulated time equals exactly the synthetic difference

### Requirement: Parallel shard logs record per-test durations

Shard processes launched by the parallel test task SHALL record per-test durations in their shard logs, so post-mortem analysis of a loaded run can identify the slowest test cases without re-running.

#### Scenario: Durations visible after a parallel run

- **WHEN** a parallel test run completes
- **THEN** each shard log contains per-test-case duration entries

### Requirement: Promptness bounds serve only as hang guards

Tests SHALL assert asynchronous shutdown correctness as completion ordering (for example, a stopped worker joins); numeric elapsed-time upper bounds, where retained, SHALL be generous watchdog values (tens of seconds), not tight responsiveness claims.

#### Scenario: Stop terminates a subprocess-waiting call

- **WHEN** a stop request is raised while an exec call is waiting on a long-running child
- **THEN** the call returns the canceled exit code (asserted by value) and joins (asserted by ordering), with the elapsed bound sized as a hang guard rather than a tight bound

### Requirement: Poll deadlines are sized to the producer's cadence

Where a test polls for an effect that a periodic producer emits, the poll deadline SHALL leave room for several producer periods under load (at least three), so the deadline remains a hang guard rather than a correctness margin. Deadlines measured in single producer periods SHALL be widened to the next order of magnitude.

#### Scenario: Waiting for a periodically flushed log line

- **WHEN** a test waits for a line that the log flusher writes once per second
- **THEN** the wait deadline allows several flush periods rather than a couple of them
- **AND** the test still fails loudly if the line never reaches disk

#### Scenario: Waiting for a gated invocation to finish

- **WHEN** a test holds an invocation at a gate while other work completes and then releases it
- **THEN** the wait for the released invocation uses a hang-guard deadline of tens of seconds

### Requirement: Concurrency and in-flight proofs are gate-based, not window-based

A test that must prove two activities overlapped SHALL prove it by holding one activity at a gate or by polling observable state, then asserting the other activity ran while it was held. A test SHALL NOT prove overlap by asserting that a second activity started within a fixed millisecond window of the first, because process spawn and scheduling latency are unbounded under a loaded parallel run.

#### Scenario: Two encodes must overlap

- **WHEN** a test asserts that two parallel encodes were in flight at the same time
- **THEN** the proof is either a gate that holds the first invocation while the second is observed, or a positive overlap assertion with no upper bound on the spawn delay
- **AND** the assertion still fails when the plan is changed to encode serially

### Requirement: Parallel shard verdicts come from the shard's own report

The parallel test task SHALL decide each shard's verdict from that shard's own test report, not from the text of its log nor from the process status the harness reports back — that status is unreliable when many processes are waited on concurrently. Each shard SHALL write a machine-readable report of its own; a shard whose report is missing, whose report is unparsable, or whose report records any failure or error SHALL be reported as failed. Shard log text MAY be quoted as supporting evidence, but a log that merely contains a failure word SHALL NOT by itself fail a shard, and a shard that dies mid-run SHALL be reported as failed even though it prints no summary.

#### Scenario: A shard fails a test case

- **WHEN** a shard's report records a failed test case
- **THEN** the parallel task reports that shard as failed, naming the failed case and the shard's log file

#### Scenario: A shard log contains the word FAILED without a failure

- **WHEN** a shard passes every test case but its log contains the word `FAILED` as part of passing output
- **THEN** the shard is reported as passed

#### Scenario: A shard crashes mid-run

- **WHEN** a shard process is terminated before it can write a complete report
- **THEN** the parallel task reports that shard as failed
- **AND** the report file for that shard is preserved for inspection

### Requirement: Shards partition the suite by an explicit case list

The parallel test task SHALL enumerate the suite's test cases itself and assign each enumerated case to exactly one shard, passing that assignment to the runner as an explicit list of case names. It SHALL NOT rely on the runner's own shard selection over a randomised execution order. The assignment SHALL be a deterministic function of the enumerated case list and the recorded per-case cost, and the task SHALL write the spec files it used for the run to disk. Enumeration SHALL read a machine-readable listing rather than the human-readable one, and case names SHALL be passed in the spec form the runner accepts, with the characters that syntax reserves escaped. Before spawning any shard the task SHALL validate the enumeration, the escaping, the scan for reserved characters and the assignment, failing with the offending count or name instead of starting a run it cannot account for. After the run, each shard's executed case count SHALL match the count assigned to it, and the task SHALL fail naming the shard when it does not. A shard whose assignment would be empty SHALL NOT be started; the task SHALL fail with guidance to lower the shard count.

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

The parallel task SHALL assign cases to shards using measured per-case cost, so that no shard carries a disproportionate share of the suite's work, and it SHALL report the resulting distribution per suite. Per-case cost SHALL be derived from the durations the runs already record, SHALL be reused across runs, and SHALL have a defined fallback for cases it does not know yet, so a first run on a fresh checkout still partitions the suite correctly.

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

The task SHALL report, per suite, an aggregate assert count equal to the sum of that suite's shards' own console totals, together with the enumerated and executed case counts. Its coverage check SHALL treat a shard that produced no readable console summary as failed, in addition to whatever that shard's report says, and the task SHALL NOT print an aggregate for such a suite. For a suite whose cases assert deterministic counts, the aggregate SHALL equal the count a single-process run of that suite prints.

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
