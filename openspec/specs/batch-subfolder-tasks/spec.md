# batch-subfolder-tasks Specification

## Purpose

Lets one encro invocation process every first-level subfolder of a root directory as an independent task — each with its own pipeline run, resumable state, archives, and status — ending in a single batch summary.

## Requirements

### Requirement: First-level subfolders are independent batch tasks

The `batch` subcommand SHALL take a root directory as its positional argument and SHALL treat every first-level subdirectory of it as an independent task. Each task SHALL run the existing single-directory pipeline with the batch invocation's processing options; one invocation SHALL apply exactly one task type (`--type video` or `--type picture` with its pack/transcode flags) uniformly to every subfolder. A root that is not a directory SHALL fail before any work starts.

#### Scenario: Picture pack batch processes each subfolder

- **WHEN** the user runs `encro batch <root> --type picture --pack` and `<root>` contains subfolders `2024-01/` and `2024-02/` with images
- **THEN** each subfolder is compressed and packed as its own pipeline run, and both produce archives

#### Scenario: Non-directory entries are ignored

- **WHEN** the root contains loose files next to subfolders
- **THEN** only the subfolders become tasks; the loose files are not processed

#### Scenario: Root that is not a directory fails

- **WHEN** the positional root argument is a regular file or does not exist
- **THEN** the command fails with an error before any subfolder is processed

#### Scenario: Root without eligible subfolders succeeds as a no-op

- **WHEN** the root exists but contains no eligible first-level subfolder after exclusions
- **THEN** the command prints a notice and exits 0

### Requirement: Batch task enumeration excludes non-task directories

Task enumeration SHALL exclude hidden directories, `.encro` work roots, and the packed output root, so a previous batch's outputs are never reprocessed as tasks.

#### Scenario: Packed output root is not a task

- **WHEN** a previous batch created `<root>/packed/` and the batch is run again
- **THEN** `packed/` is not enumerated as a task and existing archives are not reprocessed

#### Scenario: Hidden directories are not tasks

- **WHEN** the root contains a hidden subdirectory
- **THEN** that subdirectory is excluded from task enumeration

### Requirement: Subfolders without processable media are skipped

A subfolder that contains no media processable under the batch's effective option combination (task type plus its pack/transcode flags, evaluated by the same rules as a single-directory run) SHALL be skipped with a notice and SHALL NOT count as a failure.

#### Scenario: Empty subfolder is skipped

- **WHEN** a subfolder contains no media processable by the batch's effective option combination
- **THEN** a skip notice names the subfolder, processing continues with the remaining subfolders, and the exit code is 0 when no other subfolder fails

#### Scenario: Video-only subfolder is a task under video-webp

- **WHEN** the batch runs `--type picture` with video conversion enabled and a subfolder contains only videos
- **THEN** that subfolder is processed, not skipped

#### Scenario: Any file makes a subfolder a task under pack-only

- **WHEN** the batch runs in pack-only mode and a subfolder contains at least one regular file
- **THEN** that subfolder is packed, not skipped

### Requirement: Archives land in a per-subfolder output directory

Each subfolder's archives SHALL be written under `<packedRoot>/<subfolder>/`, where `<packedRoot>` defaults to `<root>/packed` and `-o/--output` overrides it as a plain directory path; the single-run output-alias forms (`+`, `=`, `input://`, `common://`) SHALL be rejected for batch. Archive naming inside a subfolder SHALL follow the existing single-run conventions unchanged.

#### Scenario: Default output layout

- **WHEN** `encro batch <root> --type picture --pack` processes subfolder `2024-01/`
- **THEN** that subfolder's archives are written under `<root>/packed/2024-01/`

#### Scenario: Output override

- **WHEN** the same batch is run with `-o <dir>`
- **THEN** archives are written under `<dir>/2024-01/` and sibling subfolders likewise

#### Scenario: Output alias forms are rejected

- **WHEN** the batch is run with `-o +` or another single-run output-alias form
- **THEN** parsing fails with an error naming the alias form as unsupported for batch

### Requirement: Each subfolder keeps an independent resumable state

Each subfolder task SHALL use its own job-state file under its own work root — created exactly when the equivalent single-directory run with the same options would create one — and the existing state matching, resume, and mismatch-discard semantics SHALL apply per subfolder independently. Re-running a batch SHALL resume each subfolder from its own saved progress without disturbing other subfolders' states.

#### Scenario: Re-run resumes the interrupted subfolder

- **WHEN** a batch is interrupted during its second subfolder and the batch is run again with the same options
- **THEN** the first subfolder performs no redundant work and the second resumes from its saved progress

#### Scenario: One subfolder's state mismatch discards only that state

- **WHEN** a saved state exists for one subfolder whose config no longer matches the current batch options while other subfolders' states match
- **THEN** only the mismatching subfolder's state is discarded and the others resume normally

### Requirement: A failed subfolder does not stop the batch

A subfolder task failure SHALL be recorded and the batch SHALL continue with the remaining subfolders. The batch exit code SHALL be non-zero exactly when at least one subfolder failed (a canceled batch keeps the cancellation exit code).

#### Scenario: Later subfolders still run after a failure

- **WHEN** subfolder 2 of 3 fails and subfolders 1 and 3 can succeed
- **THEN** subfolder 3 is still processed and the batch exits non-zero

### Requirement: Stop halts the batch at the current subfolder

A stop request SHALL let the current subfolder finish through its existing cancellation path; the remaining subfolders SHALL be marked skipped, and the batch SHALL exit with the cancellation exit code.

#### Scenario: Stop between subfolders

- **WHEN** a stop is requested while subfolder 2 of 4 is processing
- **THEN** subfolder 2 aborts safely at its cancellation point, subfolders 3 and 4 are skipped with a notice, and the exit code is the cancellation exit code

### Requirement: Batch ends with a per-subfolder summary

A finished batch SHALL print a summary table listing every enumerated subfolder with its status (done, failed, or skipped) on stdout, followed by a count line. The summary SHALL follow the console output conventions.

#### Scenario: Mixed outcomes in the summary

- **WHEN** a batch ends with one subfolder done, one failed, and one skipped
- **THEN** the summary table lists all three with their statuses and a count line names the totals

### Requirement: Batch accepts single-run processing options and rejects inapplicable ones

The batch subcommand SHALL accept the processing options of the single-directory workflow for the chosen task type (encode flags, quality, packing, parallelism, resume/restart, confirmation). It SHALL reject the multi-input option forms and an explicit single state file, and validation errors for option combinations SHALL reuse the single-run rules.

#### Scenario: Multi-input form is rejected

- **WHEN** the user runs `encro batch <root> -I <file>`
- **THEN** parsing fails with a native-style error naming the conflicting options

#### Scenario: Explicit state file is rejected

- **WHEN** the user runs `encro batch <root> --state-file <file>`
- **THEN** parsing fails with an error explaining batch keeps per-subfolder state files

### Requirement: External tools resolve once for the whole batch

The batch SHALL resolve the external toolchain (ffmpeg/ffprobe) once before processing the first subfolder — following the same per-type tool requirements as a single-directory run, including the existing pack-only exemption — share the resolution across all subfolders, and fail before any subfolder is processed when resolution fails.

#### Scenario: Missing ffmpeg fails the batch up front

- **WHEN** the batch runs with no usable ffmpeg on PATH or `--ffmpeg-path`
- **THEN** the command fails with the tool check error and no subfolder is processed
