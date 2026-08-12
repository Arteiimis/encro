# test-failure-diagnostics Specification

## Purpose

Defines the behavior contract for test infrastructure failure visibility: what a failing test run must leave behind so a developer or agent can locate and start debugging every failure.

## Requirements

### Requirement: Failing test runs produce a machine-readable report and a failure summary

The supported unit-test invocation SHALL produce a machine-readable report file containing every test result and, for each failure, the test name, source location, and failure message. When the run exits non-zero, the printed output SHALL end with a failure summary that names each failed test with its source location and failure message, so the failure information survives output truncation and is readable without re-running the suite.

#### Scenario: Run fails and summary names the failed tests

- **WHEN** the unit-test suite is run through the supported invocation and one or more tests fail
- **THEN** the invocation exits non-zero
- **AND** a report file listing all test cases with their pass/fail status exists after the run
- **AND** the tail of the printed output contains the name, source location, and failure message of each failed test

#### Scenario: Run passes and report is still produced

- **WHEN** the unit-test suite is run through the supported invocation and all tests pass
- **THEN** the report file exists and contains no failure entries
- **AND** the printed output does not contain a failure summary

### Requirement: Test runs are not polluted by progress bar frames

When a test run's stdout is not a terminal, progress bar frames SHALL NOT be emitted to stdout. Text status lines that are not bar frames SHALL continue to be emitted unchanged.

#### Scenario: Piped test output contains no bar frames

- **WHEN** the unit-test suite is run with stdout redirected to a file or pipe
- **THEN** the captured output contains no progress bar frame lines
- **AND** non-frame status lines such as completion messages are still present

#### Scenario: Interactive terminal keeps progress bars

- **WHEN** tests that exercise progress rendering run with stdout attached to a terminal
- **THEN** progress bar frames are displayed as before

### Requirement: Failed e2e assertions show the child process output

When a test asserts that a child process invocation succeeded and that assertion fails, the failure output SHALL include the child's stdout and stderr text and SHALL identify the assertion's source location (not the location of a shared helper).

#### Scenario: Child exits non-zero

- **WHEN** an e2e test asserts success of a child invocation that exits with a non-zero code
- **THEN** the test fails at the assertion's call site in the test file
- **AND** the failure output includes the child's captured stdout and stderr

### Requirement: A failing test preserves its scratch directory

When a test fails, the per-test scratch directory it created SHALL be preserved instead of deleted, and its path SHALL be printed to the standard error stream so the surviving files can be inspected. When a test passes, its scratch directory SHALL be deleted as before.

#### Scenario: Failing test keeps its files

- **WHEN** a test creates a scratch directory and subsequently fails on a fatal assertion
- **THEN** the scratch directory and its contents still exist after the test ends
- **AND** the scratch directory path appears in the test's captured error output

#### Scenario: Passing test still cleans up

- **WHEN** a test creates a scratch directory and passes
- **THEN** the scratch directory is removed after the test ends

### Requirement: Unexpected test crashes leave a crash record

When the unit-test process terminates abnormally due to an unhandled crash, a crash record SHALL be written instead of the process exiting with only a bare exit code.

#### Scenario: Test process crashes

- **WHEN** a unit test triggers an unhandled crash (e.g. invalid memory access)
- **THEN** the test process exits non-zero
- **AND** a crash record is produced containing at least the faulting context
