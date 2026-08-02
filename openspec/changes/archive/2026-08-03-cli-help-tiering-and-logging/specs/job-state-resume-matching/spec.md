## ADDED Requirements

### Requirement: Warn before discarding mismatched state

When automatic resume finds a saved state whose config does not match the current command and no explicit `--resume` was given, the run SHALL print a console warning that the saved state is discarded and a fresh run starts.

#### Scenario: Mismatched state discarded with warning
- **WHEN** a saved state exists for the input but its config does not match the current command, and `--resume` was not given
- **THEN** a warning is printed stating that the saved state does not match and is being discarded
- **AND** the run starts with a fresh snapshot as before

#### Scenario: No state file present
- **WHEN** no saved state exists for the input
- **THEN** no warning is printed

#### Scenario: Explicit --resume still errors on mismatch
- **WHEN** a saved state exists but its config does not match, and `--resume` was given
- **THEN** the run fails with the existing mismatch error (no discard, no warning)
