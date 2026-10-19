# Spec Delta

## ADDED Requirements

### Requirement: A hardening violation in a test child terminates in every build mode

The test binary SHALL NOT wait on a CRT assertion dialog: a hardening violation SHALL terminate the process in a `debug` build exactly as it does in a `release` one, so the crash-record integration test sees its record under both.

#### Scenario: Debug configuration still reports the record

- **WHEN** the suite is built for a debug mode, whose standard library debug checks report through the CRT assertion path, and a child process violates an STL precondition
- **THEN** the child terminates without opening a dialog
- **AND** the parent's crash-record assertions find the record they expect

#### Scenario: Release configuration is unchanged

- **WHEN** the same tests run under a release configuration
- **THEN** the violation still traps once and produces a single crash record
