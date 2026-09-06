# deterministic-test-sync Specification

## Purpose

Defines how the test suites synchronize with asynchronous and subprocess activity so that test outcomes depend on observable state, never on wall-clock timing assumptions, keeping `xmake test-parallel` runs deterministic under machine load.

## ADDED Requirements

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
