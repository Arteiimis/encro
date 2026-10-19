# stdlib-hardening Specification

## Purpose

Guarantees that C++ standard library precondition violations are actively checked in every release-capable build: the platform's hardening facility is enabled at compile time, violations terminate the process unrecoverably, and the test suite verifies both the activation and the violation-to-crash-record chain on every platform the suite runs on.

## Requirements

### Requirement: Windows builds enable MSVC STL hardening

Every Windows translation unit of the project SHALL be compiled with MSVC STL hardening enabled (`_MSVC_STL_HARDENING=1` visible before any standard header). A hardening check failure SHALL terminate the process immediately; it SHALL NOT be recoverable or silently ignored.

#### Scenario: Out-of-bounds vector access on Windows

- **WHEN** a Windows build accesses a vector element beyond its size
- **THEN** the process terminates at the violating call instead of reading out-of-range memory

#### Scenario: Hardening macro reaches the test suite

- **WHEN** the unit-test suite is compiled on Windows
- **THEN** the hardening macro is active in test translation units as well

### Requirement: Non-Windows builds enable libstdc++ precondition checks

Every non-Windows translation unit of the project SHALL be compiled with `_GLIBCXX_ASSERTIONS=1` (clang + libstdc++; no libc++ migration is planned). An assertion failure SHALL abort the process with the failure reason printed to the standard error stream.

#### Scenario: Out-of-bounds vector access on Linux

- **WHEN** a Linux build accesses a vector element beyond its size
- **THEN** the process aborts with a printed precondition-failure message instead of reading out-of-range memory
- **AND** the abort surfaces to the crash handler as a fatal signal, producing a crash record

#### Scenario: Hardening macro reaches the test suite on Linux

- **WHEN** the unit-test suite is compiled on Linux
- **THEN** `_GLIBCXX_ASSERTIONS` is active in test translation units as well

### Requirement: Hardening activation is verified by the test suite

The unit-test suite SHALL contain a compile-time check that the platform's hardening macro is active, so a build-configuration regression that silently disables hardening fails the suite rather than passing unnoticed. The suite SHALL also contain a runtime integration test that triggers a real STL precondition violation in a child process and asserts that the crash-record machinery produces a report; this test SHALL run on every platform where the suite runs.

#### Scenario: Compile-time activation check

- **WHEN** a build config change removes the platform's hardening define
- **THEN** the unit-test suite fails to pass its compile-time hardening check

#### Scenario: Runtime violation produces a crash record

- **WHEN** the suite's violation trigger runs in a child process
- **THEN** the child exits non-zero
- **AND** the captured output contains a crash record with a stacktrace

### Requirement: A hardening violation in a test child terminates in every build mode

The test binary SHALL NOT wait on a CRT assertion dialog: a hardening violation SHALL terminate the process in a `debug` build exactly as it does in a `release` one, so the crash-record integration test sees its record under both.

#### Scenario: Debug configuration still reports the record

- **WHEN** the suite is built for a debug mode, whose standard library debug checks report through the CRT assertion path, and a child process violates an STL precondition
- **THEN** the child terminates without opening a dialog
- **AND** the parent's crash-record assertions find the record they expect

#### Scenario: Release configuration is unchanged

- **WHEN** the same tests run under a release configuration
- **THEN** the violation still traps once and produces a single crash record
