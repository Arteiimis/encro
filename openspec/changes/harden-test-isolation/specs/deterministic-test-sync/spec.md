## ADDED Requirements

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

The parallel test task SHALL decide each shard's verdict from that shard's own test report and exit status, not from the text of its log. Each shard SHALL write a machine-readable report of its own; a shard whose report is missing or unparsable, or whose process status is non-zero, SHALL be reported as failed. Shard log text MAY be quoted as supporting evidence, but a log that merely contains a failure word SHALL NOT by itself fail a shard, and a shard that dies mid-run SHALL be reported as failed even though it prints no summary.

#### Scenario: A shard fails a test case

- **WHEN** a shard process exits non-zero and its report contains a failed test case
- **THEN** the parallel task reports that shard as failed, naming the failed case and the shard's log file

#### Scenario: A shard log contains the word FAILED without a failure

- **WHEN** a shard passes every test case but its log contains the word `FAILED` as part of passing output
- **THEN** the shard is reported as passed

#### Scenario: A shard crashes mid-run

- **WHEN** a shard process is terminated before it can write a complete report
- **THEN** the parallel task reports that shard as failed
- **AND** the report file for that shard is preserved for inspection
