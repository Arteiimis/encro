## ADDED Requirements

### Requirement: Fatal exceptions are reported under third-party crash filters

When a fatal exception (invalid memory access, illegal instruction including the standard
library hardening trap, stack overflow) is raised while a third-party top-level filter owns the
unhandled-exception path (e.g. inside a Catch2 test run), the crash report SHALL still be
written through the direct-write durability chain before that filter terminates the process.
Registering the first-chance interception SHALL NOT change the process's termination behavior:
after writing the report, the interception SHALL continue the normal exception dispatch.

#### Scenario: Hardening violation inside a test run

- **WHEN** an STL hardening violation traps during a Catch2 test execution
- **THEN** a crash record with reason and stacktrace is written (direct log append / async
  logger / stderr fallback chain, as for other crashes)
- **AND** the process still terminates non-zero through the third-party filter's normal flow

#### Scenario: Non-fatal exceptions are not intercepted

- **WHEN** a normal C++ exception (test assertion failure, error result) is thrown
- **THEN** no crash record is produced and test execution proceeds unchanged

#### Scenario: A fatal exception yields exactly one crash record

- **WHEN** a fatal exception is reported by the first-chance interception and then reaches the
  process's own unhandled-exception path
- **THEN** the crash record is written exactly once, not once per reporting layer

### Requirement: Crash records carry available process context

When a pluggable context provider has been installed, the crash record SHALL include the
context it returns (for the unit-test runner: the currently running test name). The provider
SHALL be optional; without it, crash records are written as before.

#### Scenario: Crash names the running test

- **WHEN** a fatal exception occurs while a test is running and the runner has installed a
  context provider
- **THEN** the crash record identifies that test

#### Scenario: No provider installed

- **WHEN** the context provider was never installed
- **THEN** crash records are written with reason and stacktrace as before, with no placeholder
  noise

### Requirement: Crash stacktraces resolve to symbols on Windows release builds

Windows release builds SHALL emit debug symbols alongside the binary, and crash records from
those builds SHALL resolve application stack frames to `module!function` form rather than bare
`module+offset` addresses, so a crash stack is actionable without re-running under a debugger.

#### Scenario: Release-build crash stack is readable

- **WHEN** a crash record with a stacktrace is produced by a Windows release build
- **THEN** the application frames in the record reference named functions via the debug
  symbols emitted with the build
- **AND** frames from system libraries without available symbols may remain address-only
