## MODIFIED Requirements

### Requirement: Unexpected test crashes leave a crash record

When the unit-test process terminates abnormally — due to an unhandled crash, an STL hardening
or precondition violation, or any fatal exception intercepted before the normal crash path — a
crash record SHALL be written instead of the process exiting with only a bare exit code. When
the running test is identifiable at crash time, the record SHALL name it.

#### Scenario: Test process crashes

- **WHEN** a unit test triggers an unhandled crash (e.g. invalid memory access)
- **THEN** the test process exits non-zero
- **AND** a crash record is produced containing at least the faulting context

#### Scenario: Hardening violation in a test

- **WHEN** a unit test triggers an STL hardening/assertion violation (e.g. out-of-bounds
  vector access)
- **THEN** the test process exits non-zero
- **AND** a crash record with a stacktrace is produced even though the terminating path is the
  standard library's own (trap or abort), not the unhandled-exception filter

#### Scenario: Crash record names the running test

- **WHEN** a test crashes while the suite can identify the currently running test
- **THEN** the crash record contains that test's name, so parallel-shard logs locate the
  failing test without reproducing the crash
