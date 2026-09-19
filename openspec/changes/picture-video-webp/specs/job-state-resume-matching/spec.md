## MODIFIED Requirements

### Requirement: Picture compression enables job state by default

A run with `processType=picture` and compression or video conversion enabled SHALL create and use a job state by default, without requiring `--resume` or `--restart`, matching the behavior of video runs. A picture run with neither compression nor video conversion SHALL only use job state when explicitly requested (`--resume`, `--restart`, or `--state-file`).

#### Scenario: Picture compression run creates state automatically
- **WHEN** a run processes pictures with `--compress-images` and no resume flags
- **THEN** a job state is created and persisted for the run
- **AND** the packing step of a later identical run can resume from it

#### Scenario: Video conversion alone creates state automatically
- **WHEN** a picture run converts videos with `--video-webp` and no resume flags
- **THEN** a job state is created and persisted for the run
- **AND** a later identical run can resume the per-video conversion records from it

#### Scenario: Conversion and compression together share one state
- **WHEN** a picture run enables both compression and video conversion
- **THEN** one job state is created for the run and holds both the compression phase record and the per-video conversion records

#### Scenario: Direct picture pack stays flag-gated
- **WHEN** a picture run packs with neither compression nor video conversion and without resume flags
- **THEN** no job state is created

## ADDED Requirements

### Requirement: Video conversion flag participates in state matching

The job-state config snapshot SHALL record whether video conversion is enabled, and a saved state SHALL be resumed only when that field matches the current run. A run that adds or removes `--video-webp` relative to the saved state SHALL NOT reuse it, and the existing mismatch handling applies unchanged.

#### Scenario: Adding conversion blocks resume

- **WHEN** a picture run with `-c` saved a state and the same input is now run with `-c --video-webp`
- **THEN** the saved state is not matched and the existing mismatch handling applies
- **AND** the videos are converted from an empty conversion cache

#### Scenario: Removing conversion blocks resume

- **WHEN** a picture run with `-c --video-webp` saved a state and the same input is now run with `-c` alone
- **THEN** the saved state is not matched and the existing mismatch handling applies

#### Scenario: Matching conversion flag resumes

- **WHEN** a picture run with `--video-webp` saved a state and the same command runs again
- **THEN** the saved state is matched and the already-converted videos are not converted again
