# Spec Delta

## ADDED Requirements

### Requirement: A prompt that cannot read an answer fails the run

A confirmation prompt whose input is at end-of-file SHALL NOT be reported as a user decline: the run SHALL print an error naming the unanswered question and the `--yes` remedy, SHALL NOT print a decline notice, and SHALL exit with the ordinary failure code (1).

#### Scenario: Closed stdin fails instead of declining

- **WHEN** an encode run reaches its confirmation prompt with stdin at end-of-file
- **THEN** stderr carries an error naming `--yes` and no decline notice prints
- **AND** the exit code is 1

### Requirement: A typed decline keeps its own outcome

A decline the user actually typed SHALL keep today's outcome — the decline notice prints and the run exits with the code a declined run already returns (0 when no stop is pending) — so the new no-answer path cannot change what a real decline means.

#### Scenario: A typed decline is unchanged

- **WHEN** the user answers a confirmation prompt with `n`
- **THEN** the run prints the decline notice
- **AND** the exit code stays 0 while no stop is pending
