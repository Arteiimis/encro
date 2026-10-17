# Spec Delta

## ADDED Requirements

### Requirement: Encoding settings participate in state matching

Every setting that changes the bytes of an encoded output — video codec, video quality, encoder preset and the probe quality floor — SHALL be recorded in the job-state config snapshot, and a saved state SHALL be resumed only when a recorded value equals the current run's value. A saved state that does not record a setting SHALL match any value for it, so upgrading the tool does not discard in-progress work.

#### Scenario: Changed quality is not resumed

- **WHEN** a saved state was written by a run using the default video quality and the next run passes `--crf` with a different value
- **THEN** the state does not match, the existing mismatch handling applies, and the videos are encoded again with the requested quality

#### Scenario: Identical settings resume

- **WHEN** two runs use the same quality, preset and quality floor
- **THEN** the saved state is reused and its completed tasks are recovered instead of re-encoded

#### Scenario: A state without recorded settings still resumes

- **WHEN** the saved state predates the recorded settings
- **THEN** it matches the current run and its completed tasks are recovered

### Requirement: A replacement that cannot complete keeps the previous snapshot

A completed commit SHALL leave the state path holding the new snapshot, and SHALL NOT have emptied it to get there: the staged snapshot replaces the file in one rename, and only a target the platform refuses to replace falls back to moving the current snapshot aside. When the replacement cannot complete, the previous snapshot SHALL be back at the state path and the failure SHALL be logged at error level. A successful commit SHALL leave no staging or backup file behind, so a reader that opens the state path always finds a complete, parseable snapshot.

#### Scenario: A failed replacement keeps the previous snapshot

- **WHEN** the staged snapshot cannot be moved into place
- **THEN** the previous snapshot remains at the state path and the failure is logged at error level

#### Scenario: A successful commit leaves no staging files

- **WHEN** a snapshot is committed successfully
- **THEN** the state path holds the new snapshot and no temporary or backup file remains beside it
