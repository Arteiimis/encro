## ADDED Requirements

### Requirement: Picture compression enables job state by default

A run with `processType=picture` and compression enabled SHALL create and use a job state by default, without requiring `--resume` or `--restart`, matching the behavior of video runs. A picture run without compression SHALL only use job state when explicitly requested (`--resume`, `--restart`, or `--state-file`).

#### Scenario: Picture compression run creates state automatically
- **WHEN** a run processes pictures with `--compress-images` and no resume flags
- **THEN** a job state is created and persisted for the run
- **AND** the packing step of a later identical run can resume from it

#### Scenario: Direct picture pack stays flag-gated
- **WHEN** a picture run packs without compression and without resume flags
- **THEN** no job state is created

### Requirement: Canceled compression run keeps its saved state

A canceled picture-compression run SHALL keep its saved job state and cache when cancellation happens after compression has started, so a later run resumes. Cancellation before any work started (e.g., at the confirmation prompt) SHALL remove the saved state as before.

#### Scenario: Cancel after compression started preserves state
- **WHEN** a picture-compression run is canceled while pictures are being compressed
- **THEN** the saved job state is retained
- **AND** the next run of the same command resumes compression and packing

#### Scenario: Cancel before work started removes state
- **WHEN** a picture-compression run is canceled at the confirmation prompt before any compression started
- **THEN** the saved job state is removed
- **AND** the next run starts fresh
