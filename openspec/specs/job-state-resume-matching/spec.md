# job-state-resume-matching Specification

## Purpose

Defines which saved job-state configurations a new run may resume, so users can add the pack flag on a later run of the same input without losing encode progress.

## Requirements

### Requirement: Enabling pack output on resume

A new run with `packOutput=true` SHALL be allowed to resume a saved job state whose config snapshot has `packOutput=false`, provided all other config fields match. The resumed run SHALL reuse the saved task records with the existing recovery logic (restored-from-state or restored-from-output-file), and SHALL proceed to packing as if the previous run had packing enabled.

#### Scenario: First run without packing, second run adds packing
- **WHEN** a directory was previously processed with `packOutput=false` (encode only) and the same directory is now processed with `packOutput=true`
- **THEN** the saved state is loaded instead of discarded
- **AND** encode tasks already Succeeded in the saved state are not re-encoded
- **AND** the run proceeds to the packing stage without re-encoding completed videos

#### Scenario: All encodes already complete
- **WHEN** the resumed state has all encode tasks Succeeded and `packOutput=true`
- **THEN** the run asks the user whether to proceed with packing before creating archives
- **AND** if the user declines, the run exits without packing and without touching the saved state

#### Scenario: Partial encode progress
- **WHEN** the resumed state has some encode tasks Succeeded and some Interrupted or Pending
- **THEN** only the unfinished encode tasks are re-encoded
- **AND** packing proceeds with the union of recovered and newly encoded outputs

#### Scenario: Unrelated config change still blocks resume
- **WHEN** a run adds `--pack-output` but also changes any other config field (e.g., output format, recursive, output layout, input paths)
- **THEN** the saved state is NOT matched, and the existing mismatch handling applies unchanged

### Requirement: Removing pack output on resume

A new run with `packOutput=false` SHALL NOT resume a saved job state whose config snapshot has `packOutput=true`.

#### Scenario: Packing-enabled state rejected by encode-only run
- **WHEN** a directory was previously processed with `packOutput=true` and the same directory is now processed with `packOutput=false`
- **THEN** the saved state is not considered a match (existing mismatch handling applies)

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
- **THEN** the saved state is matched and the run resumes from it, treating the saved conversion records as its own rather than discarding them
