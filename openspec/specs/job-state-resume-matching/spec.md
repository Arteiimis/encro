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
