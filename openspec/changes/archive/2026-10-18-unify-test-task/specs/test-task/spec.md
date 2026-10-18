# Spec Delta

## Purpose

Defines the project's single test entry point: how the test suites are selected, filtered and sharded, what the task guarantees about the binaries it runs, and which report artifacts a run leaves behind in every mode.

## ADDED Requirements

### Requirement: One task runs both suites

The project SHALL expose exactly one task for running its test suites, and that task SHALL select which suites run instead of requiring a second task. Suite selection SHALL offer the unit suite alone, the e2e suite alone, and both together, and an invocation that names no suite SHALL run the unit suite alone.

#### Scenario: Default invocation runs the unit suite

- **WHEN** the test task is invoked with no suite option
- **THEN** only the unit suite runs
- **AND** the e2e suite's target is not built

#### Scenario: Both suites on request

- **WHEN** the test task is invoked asking for both suites
- **THEN** both suites run in the same invocation
- **AND** the task reports one result line per suite

#### Scenario: Only the e2e suite

- **WHEN** the test task is invoked asking for the e2e suite
- **THEN** only the e2e suite runs

### Requirement: Every suite runs sharded by default

The test task SHALL shard the selected suites by default, for a single suite as well as for both, and SHALL NOT fall back to a single-process run unless the caller lowers the shard count or the enumeration is too small to fill more than one shard. Defaults SHALL be derived from the host's core count, and the caller SHALL be able to override the count for both suites at once or per suite.

#### Scenario: Default run on a multi-core host uses several shards

- **WHEN** the test task runs with no shard option on a host with several cores
- **THEN** the suite is split across more than one shard
- **AND** the task reports the shard count and the per-shard case counts it used

#### Scenario: Shard count overridden

- **WHEN** the caller passes an explicit shard count
- **THEN** that count is used for the suites it applies to
- **AND** the effective count is reported, not the requested one

#### Scenario: Single shard on request

- **WHEN** the caller asks for one shard
- **THEN** the suite runs as a single process
- **AND** its result line carries that suite's own single-process totals

#### Scenario: Defaults stay inside the host's core count

- **WHEN** both suites run with their default shard counts on a host with at least two cores
- **THEN** the number of concurrent shard processes does not exceed the host's core count
- **AND** the task reports the counts it used, so a run a caller pushed past that ceiling is recognizable
- **AND** on a host where that is impossible because both suites still need one shard each, the counts used are reported rather than silently reduced

### Requirement: A tag filter narrows the run in every mode

The test task SHALL accept a runner tag filter and apply it when enumerating the cases to run, so that a filtered run is sharded over only the selected cases. A filtered run SHALL clamp the shard count to the number of selected cases rather than fail. A filter that selects no case SHALL fail the task naming the filter, except that a suite selecting no case while another suite in the same invocation selects some SHALL be skipped with a printed notice instead.

#### Scenario: Filtered run selects few cases

- **WHEN** a tag filter selects fewer cases than the default shard count
- **THEN** the task runs fewer shards, one case per shard at most
- **AND** every selected case still runs exactly once

#### Scenario: Filter matches nothing

- **WHEN** the tag filter selects no case in any selected suite
- **THEN** the task fails and names the filter

#### Scenario: Filter selects nothing in one of two suites

- **WHEN** both suites are selected and the filter selects cases in only one of them
- **THEN** the empty suite is skipped with a notice naming it
- **AND** the other suite runs to completion

### Requirement: Report artifacts have the same paths in every mode

A run SHALL leave the same machine-readable report and console log paths for a suite whether that suite ran in one shard or many, so a post-run read does not depend on the shard count. In a sharded run those paths SHALL hold an aggregate covering every shard of the suite. Per-shard reports, logs, spec files and private temp roots SHALL remain under one run directory the task names in its output.

#### Scenario: Sharded run leaves the documented paths

- **WHEN** a suite runs across several shards
- **THEN** the suite's documented report path exists and covers every shard's results
- **AND** the suite's documented console log path exists and holds every shard's console output
- **AND** the per-shard files remain under the run directory

#### Scenario: Single-shard run leaves the same paths

- **WHEN** a suite runs as one shard
- **THEN** the same documented paths exist
- **AND** their content is that shard's report and console output

### Requirement: The task runs only freshly built binaries

Before running anything, the test task SHALL build the targets of the selected suites and SHALL abort without running tests when that build fails, so a stale binary can never produce a passing verdict.

#### Scenario: Build fails

- **WHEN** the build of a selected suite's target fails
- **THEN** the task fails naming the build step
- **AND** no test binary is run

#### Scenario: Only selected targets are built

- **WHEN** only the unit suite is selected
- **THEN** the unit test target is built
- **AND** the e2e targets are not built by the task

### Requirement: The task can check its own partitioning helpers

The test task SHALL provide a self-check mode that exercises its enumeration, escaping, partitioning, cost-model and assignment helpers against fixtures and exits without building or running any test binary.

#### Scenario: Self-check passes

- **WHEN** the self-check mode is requested
- **THEN** every helper check is reported
- **AND** the task exits successfully having run no test binary

#### Scenario: Self-check catches a broken helper

- **WHEN** a partitioning or assignment helper returns an unsound result
- **THEN** the self-check reports the failing check by name
- **AND** the task exits non-zero
