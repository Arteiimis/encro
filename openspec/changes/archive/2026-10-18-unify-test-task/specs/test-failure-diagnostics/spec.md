# Spec Delta

## MODIFIED Requirements

### Requirement: Failing test runs produce a machine-readable report and a failure summary

The supported unit-test and e2e-test invocation SHALL produce a machine-readable report file containing every test result and, for each failure, the test name, source location, and failure message. When the run exits non-zero, the printed output SHALL end with a failure summary that names each failed test with its source location and failure message, so the failure information survives output truncation and is readable without re-running the suite. The report and the summary SHALL be produced in every run mode, including a run split across shards, where the report at the documented path SHALL cover every shard and the summary SHALL name the failed case together with the file holding that shard's console output.

#### Scenario: Run fails and summary names the failed tests

- **WHEN** a test suite is run through the supported invocation and one or more tests fail
- **THEN** the invocation exits non-zero
- **AND** a report file listing all test cases with their pass/fail status exists after the run
- **AND** the tail of the printed output contains the name, source location, and failure message of each failed test

#### Scenario: Run passes and report is still produced

- **WHEN** a test suite is run through the supported invocation and all tests pass
- **THEN** the report file exists and contains no failure entries
- **AND** the printed output does not contain a failure summary

#### Scenario: Sharded run fails

- **WHEN** a suite runs split across several shards and one shard records a failed case
- **THEN** the invocation exits non-zero
- **AND** the report at the suite's documented path records that failure
- **AND** the summary names the failed case and the shard log file holding its console output

#### Scenario: The e2e suite fails

- **WHEN** the e2e suite is run through the supported invocation and one of its tests fails
- **THEN** the same report and failure-summary guarantees hold for that suite as for the unit suite
