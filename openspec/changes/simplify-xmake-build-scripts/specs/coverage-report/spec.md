# Spec Delta

## ADDED Requirements

### Requirement: The command restores the caller's build configuration

`xmake coverage` SHALL restore the build mode and the platform it found when the command started, once its report is produced — including when the run fails. Other configured options are not preserved. The `--keep` option SHALL continue to skip the restore so consecutive coverage runs can share one instrumented build.

#### Scenario: Mode restored after a successful run

- **WHEN** the user runs the coverage command while the project is configured for a mode other than `release`
- **THEN** the command instruments and runs the suites under the coverage mode
- **AND** when it finishes, the project is configured for the mode it had before the run

#### Scenario: Restore still happens after a failure

- **WHEN** the coverage run fails after it has reconfigured the project for coverage
- **THEN** the project's original mode and platform are restored anyway
- **AND** the command reports the failure and exits non-zero

#### Scenario: `--keep` leaves the coverage configuration active

- **WHEN** the user passes the option that keeps the coverage build
- **THEN** the command finishes without restoring the previous configuration
- **AND** a following coverage invocation can reuse the instrumented build
